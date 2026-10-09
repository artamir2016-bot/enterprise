// ---------------------------------------------------------------------------
// oes_query — a HEADLESS OES query runner, the engine behind the data MCP.
//
// Opens an OES FILE infobase (its active configuration + a live session), then
// reads requests from stdin — ONE request per line — and prints ONE JSON line
// per request to stdout. The OES query language (metadata-aware: catalogs,
// documents, references, dot-walk) is run through the real engine
// (ibQueryParser -> ibQueryLowering::ExecutePackage), so a model gets the SAME
// answer the platform would, not raw SQL over cryptic physical tables.
//
// Request line (newlines inside the query are encoded as the two chars \ n):
//     {"q": "SELECT Ref, Description FROM Catalog.Компоненты", "limit": 100}
//   OR the bare query text (same \n encoding), when it does not start with '{'.
// Response line:
//     {"columns":["Ref","Description"], "rows":[["...","Backend"], ...], "count":11}
//   or {"error":"<message>"}
//
// A control line {"cmd":"meta"} lists the configuration's queryable objects.
//
// Build:  cmake --build build --target oes_query
// Run:    oes_query --file E:\Projects\OES\testbase\devtracker_base
// ---------------------------------------------------------------------------

#include <cstdio>
#include <string>
#include <iostream>
#include <map>
#include <vector>

#include <wx/init.h>
#include <wx/string.h>

#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/session/sessionHolder.h"
#include "backend/session/workerPool.h"            // ibWorkerPool::RunOnSession — run the read on the session worker
#include "backend/metadataConfiguration.h"
#include "backend/metaData.h"
#include "backend/query/queryParser.h"
#include "backend/query/queryLowering.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/databaseLayer/connectionScope.h"   // bind a DB connection to this thread for the read
#include "backend/compiler/value.h"
#include "backend/backend_exception.h"

namespace {

// The live session — bound as Current() on the reading thread per query (an
// ibSessionScope, exactly as a worker does around each task), so the query engine
// sees the session's context: its connection, its runtime, its PUState. Without it
// the read has no session and comes back empty.
ibSession* g_session = nullptr;

// Minimal JSON string escape (UTF-8 bytes pass through; only the structural chars).
std::string JEsc(const wxString& s) {
	const std::string u = std::string(s.utf8_str());
	std::string out;
	out.reserve(u.size() + 8);
	for (unsigned char c : u) {
		switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
				else out += (char)c;
		}
	}
	return out;
}

// One ibValue -> a JSON scalar (string/number/bool/null). References and objects
// render as their presentation string — what a user would see in a list.
std::string ValJson(const ibValue& v) {
	switch (v.GetType()) {
		case ibValueTypes::TYPE_EMPTY:
		case ibValueTypes::TYPE_NULL:
			return "null";
		case ibValueTypes::TYPE_BOOLEAN:
			return v.GetBoolean() ? "true" : "false";
		case ibValueTypes::TYPE_NUMBER:
			return std::string(v.GetNumber().ToString().utf8_str());   // exact decimal text, a valid JSON number
		default:
			return "\"" + JEsc(v.GetString()) + "\"";
	}
}

// Decode the two-char sequence \ n (and \\) that the request line uses for newlines.
wxString DecodeQuery(const std::string& line) {
	std::string out;
	for (size_t i = 0; i < line.size(); ++i) {
		if (line[i] == '\\' && i + 1 < line.size()) {
			const char n = line[++i];
			if (n == 'n') out += '\n';
			else if (n == 't') out += '\t';
			else if (n == '\\') out += '\\';
			else { out += '\\'; out += n; }
		} else out += line[i];
	}
	return wxString::FromUTF8(out.c_str());
}

// If the line is a {"q":"..."} object, pull out q and limit by hand (no JSON lib
// needed for this tiny shape). Returns false if the line is not such an object.
bool ParseRequest(const std::string& line, wxString& query, long& limit, bool& isMeta) {
	isMeta = false;
	const size_t qpos = line.find("\"q\"");
	const size_t mpos = line.find("\"cmd\"");
	if (line.size() && line[0] == '{' && mpos != std::string::npos && line.find("meta") != std::string::npos) {
		isMeta = true; return true;
	}
	if (line.size() && line[0] == '{' && qpos != std::string::npos) {
		size_t c = line.find(':', qpos);
		size_t a = line.find('"', c + 1);
		size_t b = a + 1;
		std::string raw;
		for (; b < line.size(); ++b) {
			if (line[b] == '\\' && b + 1 < line.size()) { raw += line[b]; raw += line[b + 1]; ++b; continue; }
			if (line[b] == '"') break;
			raw += line[b];
		}
		query = DecodeQuery(raw);
		const size_t lp = line.find("\"limit\"");
		if (lp != std::string::npos) {
			long v = 0; if (std::sscanf(line.c_str() + line.find(':', lp) + 1, " %ld", &v) == 1 && v > 0) limit = v;
		}
		return true;
	}
	query = DecodeQuery(line);   // bare query text
	return true;
}

// Parse + execute + serialize. RUN ON THE SESSION WORKER (see EmitQuery): only there is
// the per-session runtime (connection, runtime image, PUState) live, so a catalog table
// actually reads its rows — on the bare main thread the scan comes back empty.
std::string BuildQueryJson(const wxString& query, long limit) {
	try {
		// Scope the query to the open configuration, else the factory falls back to the
		// global (common / plugin) sources and Catalog.<X> is "unknown metaobject kind".
		ibSourceMetaDataScope scope(ibApplicationData::GetActiveMetaData());
		ibQueryPackage pkg = ibQueryParser().ParsePackage(query);
		std::map<wxString, ibValue> params;
		std::vector<ibQueryLowering::PackageResult> results = ibQueryLowering::ExecutePackage(pkg, params);
		if (results.empty() || results.front().m_result == nullptr)
			return "{\"columns\":[],\"rows\":[],\"count\":0}";
		ibQueryLowering::PackageResult& r = results.front();
		std::string cols = "[";
		for (size_t i = 0; i < r.m_schema.size(); ++i) {
			if (i) cols += ",";
			cols += "\"" + JEsc(r.m_schema[i].m_name) + "\"";
		}
		cols += "]";

		std::string rows = "[";
		long n = 0;
		bool firstRow = true;
		while (r.m_result->Next()) {
			if (limit > 0 && n >= limit) break;
			if (!firstRow) rows += ",";
			firstRow = false;
			rows += "[";
			for (size_t i = 0; i < r.m_schema.size(); ++i) {
				if (i) rows += ",";
				const ibQueryLowering::OutputColumn& oc = r.m_schema[i];
				ibValue cell = oc.m_col != nullptr ? r.m_result->GetValue(oc.m_col)
				                                    : r.m_result->GetColumn(oc.m_alias);
				rows += ValJson(cell);
			}
			rows += "]";
			++n;
		}
		rows += "]";
		return "{\"columns\":" + cols + ",\"rows\":" + rows + ",\"count\":" + std::to_string(n) + "}";
	}
	catch (const ibBackendException& e) { return "{\"error\":\"" + JEsc(e.GetErrorDescription()) + "\"}"; }
	catch (...)                         { return "{\"error\":\"unknown exception\"}"; }
}

void EmitQuery(const wxString& query, long limit) {
	std::string out;
	ibWorkerPool* pool = g_session != nullptr ? g_session->GetWorkerPool() : nullptr;
	if (pool != nullptr)
		pool->RunOnSession(g_session, [&]() { out = BuildQueryJson(query, limit); });   // worker binds the session
	else
		out = BuildQueryJson(query, limit);
	std::printf("%s\n", out.c_str());
	std::fflush(stdout);
}

// List the configuration's queryable top-level objects + their attributes.
void EmitMeta() {
	try {
		ibMetaData* md = ibApplicationData::GetActiveMetaData();
		if (md == nullptr) { std::printf("{\"error\":\"no active metadata\"}\n"); std::fflush(stdout); return; }
		ibValueMetaObject* root = md->GetCommonMetaObject();
		std::string out = "{\"objects\":[";
		bool first = true;
		std::function<void(ibValueMetaObject*)> walk = [&](ibValueMetaObject* node) {
			if (node == nullptr) return;
			for (unsigned int i = 0; i < node->GetChildCount(); ++i) {
				ibValueMetaObject* ch = node->GetChild(i);
				if (ch == nullptr) continue;
				const wxString cls = ch->GetClassName();
				if (cls == wxT("Catalog") || cls == wxT("Document")) {
					if (!first) out += ",";
					first = false;
					// `ns` is the exact source kind to write after FROM: FROM <ns>.<name>.
					wxString ns;
					try { ns = ibValue::GetNameObjectFromID(ch->GetClassType()); } catch (...) {}
					out += "{\"name\":\"" + JEsc(ch->GetName()) + "\",\"kind\":\"" + JEsc(cls) +
					       "\",\"ns\":\"" + JEsc(ns) + "\"}";
				}
				walk(ch);
			}
		};
		walk(root);
		out += "]}";
		std::printf("%s\n", out.c_str());
	}
	catch (...) { std::printf("{\"error\":\"meta failed\"}\n"); }
	std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
	wxInitializer wxinit;
	if (!wxinit.IsOk()) { std::fprintf(stderr, "wx init failed\n"); return 1; }

	wxString baseDir;
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--file" && i + 1 < argc) baseDir = wxString::FromUTF8(argv[++i]);
	}
	if (baseDir.empty()) { std::fprintf(stderr, "usage: oes_query --file <baseDir>\n"); return 2; }

	if (!ibApplicationData::CreateFileAppDataEnv(ibRunMode::eRUNTIME_MODE, baseDir, wxT("en"))) {
		std::fprintf(stderr, "failed to open infobase: %s\n", (const char*)baseDir.utf8_str());
		return 3;
	}
	ibSessionHolder holder = appData->CreateSession();
	if (!holder || holder->Open(wxT(""), wxT("")) != ibSession::OpenResult::Authenticated) {
		std::fprintf(stderr, "failed to open session\n");
		appDataDestroy();
		return 4;
	}
	g_session = holder.Get();   // bound as Current() per read below (ibSessionScope)
	std::fprintf(stderr, "READY\n");
	std::fflush(stderr);

	std::string line;
	while (std::getline(std::cin, line)) {
		if (line.empty()) continue;
		wxString query; long limit = 1000; bool isMeta = false;
		ParseRequest(line, query, limit, isMeta);
		if (isMeta) EmitMeta();
		else        EmitQuery(query, limit);
	}

	appDataDestroy();
	return 0;
}

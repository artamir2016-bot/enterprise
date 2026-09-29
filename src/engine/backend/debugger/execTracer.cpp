////////////////////////////////////////////////////////////////////////////
//	Description : ibExecTracer — statement-level execution profiler / tracer
////////////////////////////////////////////////////////////////////////////

#include "execTracer.h"

#include <chrono>

#include "backend/compiler/codeDef.h"          // OPER_* — steppable-opcode filter
#include "backend/compiler/byteCode.h"         // ibByteUnit / ibByteCode::ibByteFunction
#include "backend/compiler/procContext.h"      // ibRunContext (m_currentFunction)
#include "backend/compiler/procUnitState.h"    // ibProcUnitState::GetCountRunContext
#include "backend/session/session.h"           // ibSession::Current / GetPUState
#include "backend/compiler/value.h"            // ibValue + g_value*CLSID
#include "backend/backend_type.h"              // ibTypeDescription
#include "backend/system/value/valueTable.h"   // ibValueModelTable (ТаблицаЗначений)

namespace {

// Same set the debugger stops on (debugServer.cpp): frame markers, param-binding
// and tape declarators carry no user-visible source position, so we don't record
// them — a statement is recorded once, on the line-change of a "real" opcode.
inline bool IsSteppableOpcode(short oper)
{
	switch (oper) {
	case OPER_FUNC:       case OPER_END:
	case OPER_SET:        case OPER_SETCONST:    case OPER_SET_TYPE:
	case OPER_TRY:        case OPER_ENDTRY:
	case OPER_FUNC_PARAM: case OPER_FUNC_LOCAL:
	case OPER_CTX_BEGIN:  case OPER_CTX_END:
		return false;
	default:
		return true;
	}
}

constexpr std::size_t kCap = 2000000;   // runaway backstop (~ bounded memory)

inline std::int64_t NowNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Fill selfMs / totalMs on a finalized copy of the rows. selfMs is the wall-clock
// gap to the NEXT recorded statement (this line's own time); totalMs is the time
// until control returns to a depth ≤ this row's (a call statement's inclusive
// cost). `endNs` closes the last row. Timing is wall-clock and INCLUDES the
// tracer's own per-line overhead, so read RELATIVE hotness, not absolute times.
void Finalize(std::vector<ibExecTracer::ibTraceRow>& rows, std::int64_t endNs)
{
	const std::size_t n = rows.size();
	for (std::size_t i = 0; i < n; ++i) {
		const std::int64_t nextNs = (i + 1 < n) ? rows[i + 1].tEnterNs : endNs;
		rows[i].selfMs = (nextNs - rows[i].tEnterNs) / 1e6;
		std::int64_t retNs = endNs;
		for (std::size_t j = i + 1; j < n; ++j) {
			if (rows[j].depth <= rows[i].depth) { retNs = rows[j].tEnterNs; break; }
		}
		rows[i].totalMs = (retNs - rows[i].tEnterNs) / 1e6;
	}
}

// Sessionless fallback collector (unit tests, bare interpreter, or any run with
// no bound session). Plus the per-thread cache of the resolved SESSION collector
// (owned by the tracer's map) so the hot path skips the map lock while the
// session on this thread stays the same.
thread_local ibExecTracer::Collector ts_sessionless;
thread_local const ibSession*        ts_cacheSess = nullptr;
thread_local ibExecTracer::Collector* ts_cachePtr = nullptr;

} // namespace

ibExecTracer* ibExecTracer::Get()
{
	static ibExecTracer s_instance;
	return &s_instance;
}

ibExecTracer::Collector* ibExecTracer::CurrentCollector(bool createForSession)
{
	ibSession* s = ibSession::Current();
	if (s == nullptr)
		return &ts_sessionless;

	if (s == ts_cacheSess && ts_cachePtr != nullptr)
		return ts_cachePtr;

	std::lock_guard<std::mutex> lock(m_mtx);
	Collector* c = nullptr;
	if (createForSession) {
		c = &m_bySession[s];   // unordered_map element pointers are stable across insert
	} else {
		auto it = m_bySession.find(s);
		c = (it != m_bySession.end()) ? &it->second : nullptr;
	}
	ts_cacheSess = s;
	ts_cachePtr  = c;
	return c;
}

void ibExecTracer::Start()
{
	Collector* c = CurrentCollector(/*createForSession*/true);
	if (c == nullptr) return;
	if (!c->active) {
		c->active = true;
		m_activeCount.fetch_add(1, std::memory_order_relaxed);
	}
	c->rows.clear();
	c->rows.reserve(1u << 16);
	c->seq = 0;
	m_enabledGlobal.store(true, std::memory_order_relaxed);
}

void ibExecTracer::Stop()
{
	Collector* c = CurrentCollector(/*createForSession*/false);
	if (c == nullptr || !c->active) return;
	c->active = false;
	if (m_activeCount.fetch_sub(1, std::memory_order_relaxed) <= 1)
		m_enabledGlobal.store(false, std::memory_order_relaxed);
}

void ibExecTracer::Record(ibRunContext* runContext, const ibByteUnit& code, long& tracePrevLine)
{
	if (!IsSteppableOpcode(code.m_numOper))
		return;
	if (static_cast<long>(code.m_numLine) == tracePrevLine)
		return;
	tracePrevLine = static_cast<long>(code.m_numLine);
	if (code.m_numLine < 0)
		return;

	Collector* c = CurrentCollector(/*createForSession*/false);
	if (c == nullptr || !c->active)
		return;
	if (c->rows.size() >= kCap)
		return;   // truncated

	ibTraceRow r;
	r.seq    = c->seq++;
	ibProcUnitState* st = ibSession::GetPUState();
	r.depth  = st != nullptr ? static_cast<int>(st->GetCountRunContext()) : 0;
	r.line   = static_cast<int>(code.m_numLine);
	r.opcode = code.m_numOper;
	r.module = code.m_strModuleName;
	r.docPath = code.m_strDocPath;
	r.func   = (runContext != nullptr && runContext->m_currentFunction != nullptr)
		? runContext->m_currentFunction->m_strRealName
		: wxString();
	r.tEnterNs = NowNs();
	c->rows.push_back(std::move(r));
}

std::vector<ibExecTracer::ibTraceRow> ibExecTracer::Snapshot() const
{
	// const, but CurrentCollector mutates the cache / map — cast away for the
	// read (the returned vector is a copy).
	Collector* c = const_cast<ibExecTracer*>(this)->CurrentCollector(false);
	if (c == nullptr) return std::vector<ibTraceRow>{};
	std::vector<ibTraceRow> out = c->rows;
	Finalize(out, NowNs());   // fill self/total timing on the copy
	return out;
}

void ibExecTracer::DropSession(const ibSession* session)
{
	std::lock_guard<std::mutex> lock(m_mtx);
	auto it = m_bySession.find(session);
	if (it == m_bySession.end()) return;
	if (it->second.active)
		m_activeCount.fetch_sub(1, std::memory_order_relaxed);
	m_bySession.erase(it);
	// Clear this thread's cache if it pointed at the erased entry (best-effort;
	// other threads re-resolve on their next access — see the note in DropSession).
	if (ts_cacheSess == session) { ts_cacheSess = nullptr; ts_cachePtr = nullptr; }
}

ibValue ibExecTracer::BuildResultTable()
{
	std::vector<ibTraceRow> rows = Snapshot();
	Stop();

	ibValueModelTable* table = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* cols = table->GetColumnCollection();

	auto addCol = [&](const wxString& name, const ibClassID& cid, const wxString& caption) -> unsigned int {
		ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* ci =
			cols->AddColumn(name, ibTypeDescription(cid), caption);
		return ci->GetColumnID();
	};

	// All columns are supplied; the VIEW (a table box on a form) hides the ones
	// the user does not want.
	const unsigned int cSeq    = addCol(wxT("НомерСтроки"),  g_valueNumberCLSID, _("#"));
	const unsigned int cDepth  = addCol(wxT("Глубина"),      g_valueNumberCLSID, _("Depth"));
	const unsigned int cModule = addCol(wxT("Модуль"),       g_valueStringCLSID, _("Module"));
	const unsigned int cFunc   = addCol(wxT("Процедура"),    g_valueStringCLSID, _("Procedure"));
	const unsigned int cLine   = addCol(wxT("СтрокаМодуля"), g_valueNumberCLSID, _("Line"));
	const unsigned int cCode   = addCol(wxT("Код"),          g_valueStringCLSID, _("Code"));
	const unsigned int cSelf   = addCol(wxT("ВремяСобственное"), g_valueNumberCLSID, _("Self ms"));
	const unsigned int cTotal  = addCol(wxT("ВремяПолное"),  g_valueNumberCLSID, _("Total ms"));
	const unsigned int cOp     = addCol(wxT("Опкод"),        g_valueNumberCLSID, _("Opcode"));

	for (const ibTraceRow& r : rows) {
		ibValueModelTable::ibValueModelTableReturnLine* line = table->GetRowAt(table->AppendRow());
		line->SetValueByMetaID(cSeq,    ibValue(static_cast<signed int>(r.seq)));
		line->SetValueByMetaID(cDepth,  ibValue(static_cast<signed int>(r.depth)));
		line->SetValueByMetaID(cModule, ibValue(r.module));
		line->SetValueByMetaID(cFunc,   ibValue(r.func));
		line->SetValueByMetaID(cLine,   ibValue(static_cast<signed int>(r.line + 1)));   // 1-based
		line->SetValueByMetaID(cCode,   ibValue(wxString()));
		line->SetValueByMetaID(cSelf,   ibValue(r.selfMs));
		line->SetValueByMetaID(cTotal,  ibValue(r.totalMs));
		line->SetValueByMetaID(cOp,     ibValue(static_cast<signed int>(r.opcode)));
		wxDELETE(line);
	}
	return table;
}

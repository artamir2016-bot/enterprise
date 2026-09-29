////////////////////////////////////////////////////////////////////////////
//	Description : ibExecTracer — statement-level execution profiler / tracer
////////////////////////////////////////////////////////////////////////////

#include "execTracer.h"

#include <vector>
#include <cstdint>
#include <chrono>

#include "backend/compiler/codeDef.h"          // OPER_* — steppable-opcode filter
#include "backend/compiler/byteCode.h"         // ibByteUnit / ibByteCode::ibByteFunction
#include "backend/compiler/procContext.h"      // ibRunContext (m_currentFunction)
#include "backend/compiler/procUnitState.h"    // ibProcUnitState::GetCountRunContext
#include "backend/session/session.h"           // ibSession::GetPUState
#include "backend/compiler/value.h"            // ibValue + g_value*CLSID
#include "backend/backend_type.h"              // ibTypeDescription
#include "backend/system/value/valueTable.h"   // ibValueModelTable (ТаблицаЗначений)

namespace {

// Per worker-thread collector — each session runs on its own thread, so this
// keeps sessions from mixing traces with no locking on the interpreter path.
// Rows are the presentation-neutral ibExecTracer::ibTraceRow (+ a wall-clock
// enter stamp kept in a parallel vector for later self/total timing).
thread_local bool                                  ts_traceActive = false;
thread_local std::vector<ibExecTracer::ibTraceRow> ts_buffer;
thread_local std::vector<std::chrono::steady_clock::time_point> ts_enter;
thread_local std::uint64_t                         ts_seq = 0;

constexpr std::size_t kCap = 2000000;   // runaway backstop (~ bounded memory)

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

} // namespace

ibExecTracer* ibExecTracer::Get()
{
	static ibExecTracer s_instance;
	return &s_instance;
}

void ibExecTracer::StartThisThread()
{
	ts_buffer.clear();
	ts_buffer.reserve(1u << 16);
	ts_enter.clear();
	ts_enter.reserve(1u << 16);
	ts_seq = 0;
	if (!ts_traceActive) {
		ts_traceActive = true;
		m_activeThreads.fetch_add(1, std::memory_order_relaxed);
	}
	m_enabledGlobal.store(true, std::memory_order_relaxed);
}

void ibExecTracer::StopThisThread()
{
	if (!ts_traceActive)
		return;
	ts_traceActive = false;
	// Clear the global gate once the last collecting thread stops.
	if (m_activeThreads.fetch_sub(1, std::memory_order_relaxed) <= 1)
		m_enabledGlobal.store(false, std::memory_order_relaxed);
}

void ibExecTracer::Record(ibRunContext* runContext, const ibByteUnit& code, long& tracePrevLine)
{
	if (!ts_traceActive)
		return;
	if (!IsSteppableOpcode(code.m_numOper))
		return;
	if (static_cast<long>(code.m_numLine) == tracePrevLine)
		return;
	tracePrevLine = static_cast<long>(code.m_numLine);
	if (ts_buffer.size() >= kCap)
		return;   // truncated — surfaced by the row count vs. seq gap

	ibExecTracer::ibTraceRow r;
	r.seq    = ts_seq++;
	ibProcUnitState* st = ibSession::GetPUState();
	r.depth  = st != nullptr ? static_cast<int>(st->GetCountRunContext()) : 0;
	r.line   = static_cast<int>(code.m_numLine);
	r.opcode = code.m_numOper;
	r.module = code.m_strModuleName;
	r.docPath = code.m_strDocPath;
	r.func   = (runContext != nullptr && runContext->m_currentFunction != nullptr)
		? runContext->m_currentFunction->m_strRealName
		: wxString();
	ts_buffer.push_back(std::move(r));
	ts_enter.push_back(std::chrono::steady_clock::now());
}

std::vector<ibExecTracer::ibTraceRow> ibExecTracer::SnapshotThisThread() const
{
	return ts_buffer;   // a copy of the calling thread's collected rows
}

ibValue ibExecTracer::BuildResultTable()
{
	StopThisThread();

	ibValueModelTable* table = new ibValueModelTable();
	ibValueModelTable::ibValueModelColumnCollection* cols = table->GetColumnCollection();

	auto addCol = [&](const wxString& name, const ibClassID& cid, const wxString& caption) -> unsigned int {
		ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* ci =
			cols->AddColumn(name, ibTypeDescription(cid), caption);
		return ci->GetColumnID();
	};

	// All columns are supplied; the VIEW (a table box on a form) hides the ones
	// the user does not want. `Код` (source line text) lands in a later increment.
	const unsigned int cSeq    = addCol(wxT("НомерСтроки"),  g_valueNumberCLSID, _("#"));
	const unsigned int cDepth  = addCol(wxT("Глубина"),      g_valueNumberCLSID, _("Depth"));
	const unsigned int cModule = addCol(wxT("Модуль"),       g_valueStringCLSID, _("Module"));
	const unsigned int cFunc   = addCol(wxT("Процедура"),    g_valueStringCLSID, _("Procedure"));
	const unsigned int cLine   = addCol(wxT("СтрокаМодуля"), g_valueNumberCLSID, _("Line"));
	const unsigned int cCode   = addCol(wxT("Код"),          g_valueStringCLSID, _("Code"));
	const unsigned int cOp     = addCol(wxT("Опкод"),        g_valueNumberCLSID, _("Opcode"));

	for (const ibExecTracer::ibTraceRow& r : ts_buffer) {
		ibValueModelTable::ibValueModelTableReturnLine* line = table->GetRowAt(table->AppendRow());
		line->SetValueByMetaID(cSeq,    ibValue(static_cast<signed int>(r.seq)));
		line->SetValueByMetaID(cDepth,  ibValue(static_cast<signed int>(r.depth)));
		line->SetValueByMetaID(cModule, ibValue(r.module));
		line->SetValueByMetaID(cFunc,   ibValue(r.func));
		line->SetValueByMetaID(cLine,   ibValue(static_cast<signed int>(r.line + 1)));   // 1-based for display
		line->SetValueByMetaID(cCode,   ibValue(wxString()));
		line->SetValueByMetaID(cOp,     ibValue(static_cast<signed int>(r.opcode)));
		wxDELETE(line);
	}

	ts_buffer.clear();
	ts_enter.clear();
	return table;
}

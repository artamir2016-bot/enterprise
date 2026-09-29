// =============================================================================
// OES Enterprise — execution profiler / tracer (ibExecTracer) tests
//
// Drives the tracer's C++ API directly around a real ibProcUnit::Execute (no
// session / open configuration needed — the sessionless fallback ibProcUnitState
// supplies call depth, and the trace is read through SnapshotThisThread, which
// has no value-table / metadata dependency). Verifies the trace is recorded in
// execution order, that call depth rises across a nested call, and that both
// functions appear — i.e. the "code as it executed" sequence a developer needs
// to see non-obvious flow.
// =============================================================================

#include <gtest/gtest.h>

#include <vector>

#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnit.h"
#include "backend/compiler/byteCode.h"
#include "backend/compiler/value.h"
#include "backend/debugger/execTracer.h"

namespace {

::testing::AssertionResult CompileOk(ibCompileCode& cc, const wxString& src) {
	try {
		if (cc.Compile(src))
			return ::testing::AssertionSuccess();
		return ::testing::AssertionFailure() << "Compile() returned false";
	} catch (const ibBackendException& err) {
		return ::testing::AssertionFailure() << err.GetErrorDescription().ToStdString();
	} catch (...) {
		return ::testing::AssertionFailure() << "unknown exception";
	}
}

} // namespace

// A nested call: Outer(y) → Inner(y). The tracer must record Outer's lines, then
// Inner's at a DEEPER call level, then Outer's tail — the execution sequence.
TEST(ExecTracer, RecordsExecutionSequenceWithDepth) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function Inner(x) Public\n")
		wxT("  Return x * 2;\n")
		wxT("EndFunction\n")
		wxT("Function Outer(y) Public\n")
		wxT("  z = Inner(y);\n")
		wxT("  Return z + 1;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(CompileOk(cc, src));

	ibProcUnit pu;
	pu.Execute(cc.m_cByteCode);            // init the module (declare functions)

	execTracer->Start();
	ibValue ret;
	ibValue arg(21);
	pu.CallAsFunc(wxT("Outer"), ret, arg);  // Inner(21)=42, +1 = 43
	std::vector<ibExecTracer::ibTraceRow> rows = execTracer->Snapshot();
	execTracer->Stop();

	EXPECT_EQ(ret.GetInteger(), 43);
	ASSERT_GT(rows.size(), 0u);

	long minDepth = 1 << 30, maxDepth = 0;
	long long prevSeq = -1;
	bool seqMonotonic = true, sawInner = false, sawOuter = false;
	for (const ibExecTracer::ibTraceRow& r : rows) {
		if (static_cast<long long>(r.seq) <= prevSeq) seqMonotonic = false;
		prevSeq = static_cast<long long>(r.seq);
		if (r.depth < minDepth) minDepth = r.depth;
		if (r.depth > maxDepth) maxDepth = r.depth;
		if (r.func == wxT("Inner")) sawInner = true;
		if (r.func == wxT("Outer")) sawOuter = true;
	}

	EXPECT_TRUE(seqMonotonic);        // recorded strictly in execution order
	EXPECT_TRUE(sawOuter);            // the caller's lines are present
	EXPECT_TRUE(sawInner);            // the nested call's lines are present
	EXPECT_GT(maxDepth, minDepth);    // call depth rises entering Inner
}

// When the tracer is not started, the hot-path gate stays off and NOTHING new is
// collected while code runs (the thread-local buffer may still hold a prior run's
// rows — Stop does not clear — so we assert the count does not GROW).
TEST(ExecTracer, DisabledCollectsNothingNew) {
	execTracer->Stop();   // ensure this thread is not collecting
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(CompileOk(cc, wxT("Function F(x) Public\n  Return x + 1;\nEndFunction\n")));

	ibProcUnit pu;
	pu.Execute(cc.m_cByteCode);
	ibValue ret, arg(1);

	const size_t before = execTracer->Snapshot().size();
	pu.CallAsFunc(wxT("F"), ret, arg);      // runs with tracer OFF
	const size_t after = execTracer->Snapshot().size();
	EXPECT_EQ(after, before);
}

// Start clears the previous run's rows — a second measurement is independent.
TEST(ExecTracer, StartClearsPreviousRun) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(CompileOk(cc, wxT("Function G(x) Public\n  y = x + 1;\n  Return y + 1;\nEndFunction\n")));
	ibProcUnit pu;
	pu.Execute(cc.m_cByteCode);
	ibValue ret, arg(1);

	execTracer->Start();
	pu.CallAsFunc(wxT("G"), ret, arg);
	const size_t first = execTracer->Snapshot().size();
	EXPECT_GT(first, 0u);

	execTracer->Start();          // clears
	EXPECT_EQ(execTracer->Snapshot().size(), 0u);
	pu.CallAsFunc(wxT("G"), ret, arg);
	const size_t second = execTracer->Snapshot().size();
	execTracer->Stop();
	EXPECT_EQ(second, first);               // same code → same number of statements
}

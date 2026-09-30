#include "crashGuard.h"

#include "backend/backend_exception.h"

#include <wx/datetime.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/thread.h>
#include <wx/utils.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>

#ifdef __WXMSW__
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
// POSIX backtrace — glibc + macOS libSystem. FreeBSD same header.
// Wrap behind a feature check if a future port lacks it.
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ibCrashGuard {

namespace {

// Where dumps land. Resolved once on Install so the SEH/signal
// handlers agree on a single path even after cwd changes.
wxString s_crashDir;
wxString s_exeName;

unsigned int CurrentPidPortable()
{
	return static_cast<unsigned int>(wxGetProcessId());
}

unsigned long CurrentTidPortable()
{
	return static_cast<unsigned long>(wxThread::GetCurrentId());
}

#ifdef __WXMSW__
LPTOP_LEVEL_EXCEPTION_FILTER s_prevSehFilter = nullptr;
#endif
std::terminate_handler       s_prevTerminate = nullptr;
std::atomic<bool>            s_installed{ false };

void EnsureCrashDir()
{
	if (s_crashDir.IsEmpty()) {
		const wxString exePath = wxStandardPaths::Get().GetExecutablePath();
		s_crashDir = wxFileName(exePath).GetPath()
			+ wxFILE_SEP_PATH + wxT("crashdumps");
	}
	wxFileName::Mkdir(s_crashDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
}

// WINDOWS ONLY, and declared under the same guard as its single caller (the minidump writer).
// The POSIX handler builds its path with snprintf into a stack buffer, because it runs in a signal
// context where wxString is not allowed — so off MSW this function has no user at all.
#ifdef __WXMSW__
wxString MakeDumpPath(const wxString& kindSuffix, const wxString& extension)
{
	EnsureCrashDir();
	// Stamp + pid + tid + counter — two threads crashing in the same
	// second used to land on the same filename and the second overwrote
	// the first. tid disambiguates the common case; the atomic counter
	// handles two faults on the same thread within one second.
	static std::atomic<unsigned> s_seq{ 0 };
	const wxString stamp = wxDateTime::Now().Format(wxT("%Y%m%dT%H%M%S"));
	const unsigned seq = s_seq.fetch_add(1, std::memory_order_relaxed);
	return wxString::Format(wxT("%s%c%s_%s%u_t%lu_%s_%u.%s"),
		s_crashDir, wxFILE_SEP_PATH,
		s_exeName,
		kindSuffix,
		CurrentPidPortable(),
		CurrentTidPortable(),
		stamp,
		seq,
		extension);
}
#endif // __WXMSW__

void LogTerminateReason(const wxString& reason)
{
	// Multiple background threads can hit terminate concurrently and
	// each one's reason should land in the log, not clobber the previous.
	// Mutex guards format-then-write — OS-level append-write is atomic
	// but a wxString::Format followed by wxFile::Write is not.
	static std::mutex s_logMtx;
	std::lock_guard<std::mutex> lk(s_logMtx);

	EnsureCrashDir();
	const wxString logPath = wxString::Format(wxT("%s%c%s_terminate_%u.log"),
		s_crashDir, wxFILE_SEP_PATH,
		s_exeName,
		CurrentPidPortable());
	wxFile f(logPath, wxFile::write_append);
	if (f.IsOpened()) {
		const wxString line = wxString::Format(wxT("%s  tid=%lu  %s\n"),
			wxDateTime::Now().FormatISOCombined(),
			CurrentTidPortable(),
			reason);
		f.Write(line);
		f.Close();
	}
}

#ifdef __WXMSW__
// Write a SYMBOLISED backtrace of the faulting thread next to the minidump, so a
// crash can be diagnosed from the log alone (no cdb / VS needed). Best-effort: any
// dbghelp failure just yields fewer frames. Uses the same dbghelp already linked for
// MiniDumpWriteDump. dbghelp is single-threaded — a crash filter is the one place we
// don't contend, but guard with a flag so re-entrancy can't loop.
void WriteCrashStack(EXCEPTION_POINTERS* ep, const wxString& stackPath)
{
	if (ep == nullptr || ep->ContextRecord == nullptr) return;

	wxFile f(stackPath, wxFile::write);
	if (!f.IsOpened()) return;

	const HANDLE proc = ::GetCurrentProcess();
	const HANDLE thread = ::GetCurrentThread();

	::SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
	::SymInitialize(proc, nullptr, TRUE);

	// Exception header — code + faulting address.
	const EXCEPTION_RECORD* er = ep->ExceptionRecord;
	f.Write(wxString::Format(wxT("exception 0x%08X at %p\n"),
		er ? (unsigned)er->ExceptionCode : 0u,
		er ? er->ExceptionAddress : nullptr));
	if (er && er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
		f.Write(wxString::Format(wxT("access violation %s address %p\n"),
			er->ExceptionInformation[0] == 1 ? wxT("WRITING") : wxT("reading"),
			(void*)er->ExceptionInformation[1]));
	}

	CONTEXT ctx = *ep->ContextRecord;   // StackWalk64 mutates the context — copy it
	STACKFRAME64 frame = {};
#if defined(_M_X64)
	frame.AddrPC.Offset    = ctx.Rip; frame.AddrPC.Mode    = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
	const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#else
	const DWORD machine = IMAGE_FILE_MACHINE_I386;
#endif

	alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 512] = {};
	SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
	sym->SizeOfStruct = sizeof(SYMBOL_INFO);
	sym->MaxNameLen   = 511;

	for (int i = 0; i < 64; ++i) {
		if (!::StackWalk64(machine, proc, thread, &frame, &ctx,
			nullptr, ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr))
			break;
		const DWORD64 addr = frame.AddrPC.Offset;
		if (addr == 0) break;

		wxString line = wxString::Format(wxT("  #%02d 0x%016I64X"), i, addr);

		DWORD64 disp = 0;
		if (::SymFromAddr(proc, addr, &disp, sym))
			line += wxString::Format(wxT("  %s +0x%I64X"), wxString::FromUTF8(sym->Name), disp);

		IMAGEHLP_MODULE64 mod = {}; mod.SizeOfStruct = sizeof(mod);
		if (::SymGetModuleInfo64(proc, addr, &mod))
			line += wxString::Format(wxT("  [%s]"), wxString::FromUTF8(mod.ModuleName));

		IMAGEHLP_LINE64 il = {}; il.SizeOfStruct = sizeof(il); DWORD col = 0;
		if (::SymGetLineFromAddr64(proc, addr, &col, &il))
			line += wxString::Format(wxT("  %s:%lu"), wxString::FromUTF8(il.FileName), il.LineNumber);

		f.Write(line + wxT("\n"));
	}
	f.Close();
	::SymCleanup(proc);
}

LONG WINAPI PersistentCrashDumpFilter(EXCEPTION_POINTERS* ep)
{
	// Persistent minidump fires before any wx-level dialog. wx wipes
	// its temp directory on dialog close; our dumps survive.
	const wxString dumpPath = MakeDumpPath(wxEmptyString, wxT("dmp"));

	HANDLE hFile = ::CreateFileW(dumpPath.wc_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION mei = {};
		mei.ThreadId = ::GetCurrentThreadId();
		mei.ExceptionPointers = ep;
		mei.ClientPointers = FALSE;

		const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
			MiniDumpWithDataSegs |
			MiniDumpWithHandleData |
			MiniDumpWithUnloadedModules |
			MiniDumpWithThreadInfo |
			MiniDumpWithFullMemory);

		::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(),
			hFile, type, ep ? &mei : nullptr, nullptr, nullptr);
		::CloseHandle(hFile);
	}

	// Symbolised backtrace beside the dump — lets a crash be diagnosed from the log
	// alone (no external debugger). Best-effort; guarded against re-entrancy.
	static std::atomic<bool> s_inStack{ false };
	bool expected = false;
	if (s_inStack.compare_exchange_strong(expected, true)) {
		WriteCrashStack(ep, MakeDumpPath(wxEmptyString, wxT("stack.txt")));
		s_inStack.store(false);
	}

	// Chain to the previous filter (wx's, if frontend installed it).
	return s_prevSehFilter ? s_prevSehFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}
#else
// POSIX signal handler — async-signal-safe. open + write + close +
// backtrace_symbols_fd are explicitly allowed by POSIX; wxString /
// wxFile / wxLog are NOT.
void PosixCrashSignalHandler(int sig)
{
	char path[1024];
	const char* dir = s_crashDir.empty()
		? "crashdumps"
		: s_crashDir.ToUTF8().data();
	std::snprintf(path, sizeof(path), "%s/%s_signal_%u_t%lu.log",
		dir,
		s_exeName.IsEmpty() ? "oes" : (const char*)s_exeName.ToUTF8(),
		CurrentPidPortable(),
		CurrentTidPortable());

	const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd >= 0) {
		char hdr[256];
		const int n = std::snprintf(hdr, sizeof(hdr),
			"signal=%d  pid=%u  tid=%lu\n",
			sig, CurrentPidPortable(), CurrentTidPortable());
		// The result is deliberately not acted on: this runs inside a signal handler for a crash
		// already in progress, and there is no second way to report a failed write. Named rather
		// than dropped, because glibc marks write() warn_unused_result.
		if (n > 0) {
			const ssize_t written = ::write(fd, hdr, static_cast<size_t>(n));
			(void)written;
		}

		void* frames[64];
		const int nf = ::backtrace(frames, 64);
		::backtrace_symbols_fd(frames, nf, fd);
		::close(fd);
	}

	// Re-raise with the default handler — produces a core dump and lets
	// any chained handler (frontend's wx hook) still run on main thread.
	std::signal(sig, SIG_DFL);
	std::raise(sig);
}
#endif

#ifdef __WXMSW__
void __cdecl OesTerminateHandler()
#else
void OesTerminateHandler()
#endif
{
	// Worker / registry / debug-listener threads bypass wxApp's
	// OnUnhandledException — that only catches escapes from the wx
	// main event loop. A C++ throw unwinding off any other thread
	// hits std::terminate, which default-aborts without firing any
	// of our diagnostic chains. We capture the in-flight exception
	// text into a log, then raise an OS-level fault so the platform's
	// crash-dump path produces an artefact against this thread.
	//
	// Outer try / catch: if any step inside the handler itself throws
	// (bad_alloc on rethrow_exception, etc.) we MUST NOT let it
	// re-enter std::terminate — that's an infinite recursion on most
	// platforms. Swallow and fall through to the raise below.
	try {
		wxString reason = wxT("std::terminate (no in-flight exception)");
		try {
			auto p = std::current_exception();
			if (p) std::rethrow_exception(p);
		}
		catch (const ibBackendException& e) {
			reason = wxT("ibBackendException: ") + e.GetErrorDescription();
		}
		catch (const std::exception& e) {
			reason = wxT("std::exception: ") + wxString::FromUTF8(e.what());
		}
		catch (...) {
			reason = wxT("unknown C++ exception");
		}

		LogTerminateReason(reason);
	}
	catch (...) {
	}

#ifdef __WXMSW__
	// Synthetic non-continuable SEH so PersistentCrashDumpFilter writes
	// a dump on this thread. 0xE0E50001 — 'OE' magic + sub-code.
	::RaiseException(0xE0E50001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#else
	std::raise(SIGABRT);
#endif

	if (s_prevTerminate) s_prevTerminate();
	std::abort();
}

// Automation / background mode — suppress user-visible error dialogs (oesApp.h reads this).
std::atomic<bool> s_suppressDialogs{ false };

} // namespace

void Install(const wxString& exeName)
{
	// Update the label even on repeat install — frontend might call
	// after console / web layer already armed the handlers.
	s_exeName = exeName;

	if (s_installed.exchange(true, std::memory_order_acq_rel))
		return;

	if (s_prevTerminate == nullptr)
		s_prevTerminate = std::set_terminate(&OesTerminateHandler);

#ifdef __WXMSW__
	if (s_prevSehFilter == nullptr)
		s_prevSehFilter = ::SetUnhandledExceptionFilter(&PersistentCrashDumpFilter);
#else
	struct sigaction sa;
	std::memset(&sa, 0, sizeof(sa));
	sa.sa_handler = &PosixCrashSignalHandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	::sigaction(SIGSEGV, &sa, nullptr);
	::sigaction(SIGABRT, &sa, nullptr);
	::sigaction(SIGFPE,  &sa, nullptr);
	::sigaction(SIGILL,  &sa, nullptr);
	::sigaction(SIGBUS,  &sa, nullptr);
#endif
}

void LogStartupError(const wxString& exeName, const wxString& message)
{
	const wxString logPath = exeName + wxT("_startup.log");
	wxFile f(logPath, wxFile::write_append);
	if (f.IsOpened()) {
		const wxString line = wxDateTime::Now().FormatISOCombined()
			+ wxT("  ") + message + wxT("\n");
		f.Write(line);
		f.Close();
	}
}

void LogUnhandledException(const wxString& exeName, const wxString& diag)
{
	const wxString logPath = exeName + wxT("_unhandled.log");
	wxFile f(logPath, wxFile::write_append);
	if (f.IsOpened()) {
		f.Write(wxDateTime::Now().FormatISOCombined()
			+ wxT("  ") + diag + wxT("\n"));
		f.Close();
	}
}

void PlatformNativeMessage(const wxString& caption, const wxString& message)
{
#ifdef __WXMSW__
	// MessageBoxW doesn't depend on the wx event loop being up. Safer
	// than wxMessageBox during startup / on background threads / after
	// teardown.
	::MessageBoxW(NULL, message.wc_str(), caption.wc_str(),
		MB_OK | MB_ICONERROR | MB_TASKMODAL);
#else
	// stderr — async-signal-safe enough, no GUI to drive in console mode.
	std::fprintf(stderr, "[%s] %s\n",
		(const char*)caption.ToUTF8(),
		(const char*)message.ToUTF8());
	std::fflush(stderr);
#endif
}

void TerminateProcessFast(int exitCode)
{
#ifdef __WXMSW__
	::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(exitCode));
#else
	std::_Exit(exitCode);
#endif
}

int WrapStartup(const wxString& exeName, std::function<int()> body)
{
	auto report = [&exeName](const wxString& msg) {
		LogStartupError(exeName, msg);
		PlatformNativeMessage(wxT("OES ") + exeName + wxT(" - startup error"), msg);
	};
	try {
		return body();
	}
	catch (const ibBackendException& e) {
		report(wxT("ibBackendException during startup: ") + e.GetErrorDescription());
		return 1;
	}
	catch (const std::exception& e) {
		report(wxT("std::exception during startup: ") + wxString::FromUTF8(e.what()));
		return 1;
	}
	catch (...) {
		report(wxT("Unknown exception during startup (non-std, non-ibBackend)"));
		return 1;
	}
}

wxString GetCrashDir()
{
	EnsureCrashDir();
	return s_crashDir;
}

void SetSuppressDialogs(bool suppress) { s_suppressDialogs.store(suppress, std::memory_order_relaxed); }
bool SuppressDialogs() { return s_suppressDialogs.load(std::memory_order_relaxed); }

} // namespace ibCrashGuard

#include "backend_mainFrame.h"

// ibBackendDocFrame no longer owns any process-level / thread-local
// state — the frame is a member of the ibSession that created it.
// ms_mainFrame / t_mainFrame / GetDocMDIFrame / InstallOnThread and the
// transitional ibCurrentFrame() helper are all gone. Callers reach
// their frame through the session pointer that's already in scope.
// Default ctor/dtor live in the header.

int ibBackendDocFrame::ShowModalMessage(const wxString& message,
                                           const wxString& caption,
                                           int style)
{
	// OES-TEST: a registered interceptor answers without any UI (headless / test capture).
	int answer = 0;
	if (RunModalInterceptor(message, caption, style, answer))
		return answer;
	// Default base body — no UI, so nothing to show. Returns 0 which
	// maps to wxCANCEL/"aborted" in Question-style callers.
	return 0;
}

// OES-TEST: process-wide modal interceptor storage. Set by the test agent; consulted by every
// ShowModalMessage override (base + desktop) so a modal never blocks an automated run.
static ibBackendDocFrame::ibModalInterceptor gs_modalInterceptor;

void ibBackendDocFrame::SetModalInterceptor(ibBackendDocFrame::ibModalInterceptor fn)
{
	gs_modalInterceptor = std::move(fn);
}

bool ibBackendDocFrame::RunModalInterceptor(const wxString& message, const wxString& caption,
	int style, int& answer)
{
	if (!gs_modalInterceptor)
		return false;
	return gs_modalInterceptor(message, caption, style, answer);
}

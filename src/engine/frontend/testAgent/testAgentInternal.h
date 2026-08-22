#ifndef __OES_TEST_AGENT_INTERNAL_H__
#define __OES_TEST_AGENT_INTERNAL_H__

// OES-TEST: internal dispatch surface shared between the agent's transport (testAgentCommands.cpp)
// and the enterprise runtime-form command set (testAgentForms.cpp). Not part of the public API.

#include "../../../3rdparty/nlohmann/json.hpp"
#include <string>

// Handle a form/enterprise command. Returns true if `cmd` is one of ours (fills `result`; may throw
// std::runtime_error on failure, which the caller turns into an {"ok":false,"error"} response).
// Returns false if `cmd` is unknown here, so the caller can report "unknown command".
bool ibTestAgentDispatchForm(const std::string& cmd, const nlohmann::json& args, nlohmann::json& result);

// Install the user-message capture tap (idempotent). Called from ibTestAgent::Start().
void ibTestAgentInstallMessageTap();

#endif // __OES_TEST_AGENT_INTERNAL_H__

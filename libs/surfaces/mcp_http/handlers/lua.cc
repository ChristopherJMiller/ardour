/*
 * Copyright (C) 2026 Christopher Miller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include "ardour/luabindings.h"
#include "ardour/luascripting.h"
#include "ardour/session.h"

#include "lua/luastate.h"

#include "handlers/common.h"
#include "handlers/lua.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

const char*
script_type_string (ARDOUR::LuaScriptInfo::ScriptType t)
{
	switch (t) {
		case ARDOUR::LuaScriptInfo::DSP:           return "dsp";
		case ARDOUR::LuaScriptInfo::Session:       return "session";
		case ARDOUR::LuaScriptInfo::EditorHook:    return "hook";
		case ARDOUR::LuaScriptInfo::EditorAction:  return "action";
		case ARDOUR::LuaScriptInfo::Snippet:       return "snippet";
		case ARDOUR::LuaScriptInfo::SessionInit:   return "session_init";
		default: return "unknown";
	}
}

void
emit_script_info (std::ostringstream& ss, const ARDOUR::LuaScriptInfoPtr& info, bool& first)
{
	if (!first) {
		ss << ",";
	}
	first = false;
	ss << "{\"type\":\"" << script_type_string (info->type) << "\""
	   << ",\"name\":\"" << json_escape (info->name) << "\""
	   << ",\"uniqueId\":\"" << json_escape (info->unique_id) << "\""
	   << ",\"path\":\"" << json_escape (info->path) << "\""
	   << ",\"author\":\"" << json_escape (info->author) << "\""
	   << ",\"category\":\"" << json_escape (info->category) << "\""
	   << ",\"description\":\"" << json_escape (info->description) << "\""
	   << "}";
}

std::string
handle_lua_list_scripts_tool (ARDOUR::Session& /*session*/, const std::string& id)
{
	ARDOUR::LuaScripting& ls = ARDOUR::LuaScripting::instance ();

	std::ostringstream ss;
	ss << "{\"scripts\":[";
	bool first = true;

	for (const ARDOUR::LuaScriptInfoPtr& info : ls.scripts (ARDOUR::LuaScriptInfo::EditorAction)) {
		emit_script_info (ss, info, first);
	}
	for (const ARDOUR::LuaScriptInfoPtr& info : ls.scripts (ARDOUR::LuaScriptInfo::Snippet)) {
		emit_script_info (ss, info, first);
	}
	for (const ARDOUR::LuaScriptInfoPtr& info : ls.scripts (ARDOUR::LuaScriptInfo::Session)) {
		emit_script_info (ss, info, first);
	}
	for (const ARDOUR::LuaScriptInfoPtr& info : ls.scripts (ARDOUR::LuaScriptInfo::SessionInit)) {
		emit_script_info (ss, info, first);
	}

	ss << "]}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Lua scripts listed\"}],\"structuredContent\":") + ss.str () + "}");
}

/* Lazy-initialized Lua state, owned by the surface. Created on first eval and
 * reused so script state persists across calls within a session.
 */
std::mutex                 lua_state_mutex;
std::unique_ptr<LuaState>  shared_lua_state;
ARDOUR::Session*           shared_lua_session_bound = 0;

LuaState&
shared_state (ARDOUR::Session& session)
{
	std::lock_guard<std::mutex> lock (lua_state_mutex);
	if (!shared_lua_state || shared_lua_session_bound != &session) {
		shared_lua_state.reset (new LuaState (false /*sandbox*/, false /*rt_safe*/));
		lua_State* L = shared_lua_state->getState ();
		ARDOUR::LuaBindings::stddef (L);
		ARDOUR::LuaBindings::common (L);
		ARDOUR::LuaBindings::non_rt (L);
		ARDOUR::LuaBindings::osc (L);
		ARDOUR::LuaBindings::set_session (L, &session);
		shared_lua_session_bound = &session;
	}
	return *shared_lua_state;
}

std::string
handle_lua_eval_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string source = root.get<std::string> ("params.arguments.source", "");
	if (source.empty ()) {
		return jsonrpc_error (id, -32602, "Missing source (Lua code to evaluate)");
	}

	LuaState&         state = shared_state (session);
	std::string       captured;
	sigc::connection  conn = state.Print.connect (
	    [&captured] (std::string s) { captured += s; if (!s.empty () && s.back () != '\n') captured += '\n'; });

	const int rc = state.do_command (source);
	conn.disconnect ();

	std::ostringstream ss;
	ss << "{\"ok\":" << (rc == 0 ? "true" : "false")
	   << ",\"returnCode\":" << rc
	   << ",\"output\":\"" << json_escape (captured) << "\""
	   << "}";

	if (rc != 0) {
		return jsonrpc_error (id, -32000, std::string ("Lua error (rc=") + std::to_string (rc) + "): " + captured);
	}

	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + json_escape (captured.empty () ? std::string ("(no output)") : captured) + "\"}],\"structuredContent\":" + ss.str () + "}");
}

} /* anonymous namespace */

bool
dispatch_lua_tool_call (ARDOUR::Session& session, const std::string& tool_name, const pt::ptree& root, const std::string& id, std::string& response)
{
	if (tool_name == "lua/list_scripts") {
		response = handle_lua_list_scripts_tool (session, id);
		return true;
	}
	if (tool_name == "lua/eval") {
		response = handle_lua_eval_tool (session, root, id);
		return true;
	}

	return false;
}

} /* namespace mcp */
} /* namespace ArdourSurface */

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

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "evoral/Parameter.h"

#include "ardour/automation_control.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/types.h"

#include "handlers/automation.h"
#include "handlers/common.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

struct ParamMap {
	const char*           name;
	ARDOUR::AutomationType type;
};

/* Friendly wire names for route-level automation parameters. Plugin
 * automation is intentionally out of scope for this minimal handler.
 */
static const ParamMap kParamMap[] = {
	{ "gain",         ARDOUR::GainAutomation         },
	{ "pan",          ARDOUR::PanAzimuthAutomation   },
	{ "pan_azimuth",  ARDOUR::PanAzimuthAutomation   },
	{ "pan_width",    ARDOUR::PanWidthAutomation     },
	{ "mute",         ARDOUR::MuteAutomation         },
	{ "solo",         ARDOUR::SoloAutomation         },
	{ "rec_enable",   ARDOUR::RecEnableAutomation    },
	{ "rec_safe",     ARDOUR::RecSafeAutomation      },
	{ "trim",         ARDOUR::TrimAutomation         },
	{ "phase",        ARDOUR::PhaseAutomation        },
	{ "monitoring",   ARDOUR::MonitoringAutomation   },
};

std::string
auto_state_string (ARDOUR::AutoState s)
{
	switch (s) {
		case ARDOUR::Off:   return "off";
		case ARDOUR::Play:  return "play";
		case ARDOUR::Write: return "write";
		case ARDOUR::Touch: return "touch";
		case ARDOUR::Latch: return "latch";
	}
	return "off";
}

bool
parse_auto_state (std::string s, ARDOUR::AutoState& out, std::string& error)
{
	std::transform (s.begin (), s.end (), s.begin (), [] (unsigned char c) { return std::tolower (c); });
	if (s == "off")   { out = ARDOUR::Off;   return true; }
	if (s == "play")  { out = ARDOUR::Play;  return true; }
	if (s == "write") { out = ARDOUR::Write; return true; }
	if (s == "touch") { out = ARDOUR::Touch; return true; }
	if (s == "latch") { out = ARDOUR::Latch; return true; }
	error = "Invalid state (expected: off, play, write, touch, latch)";
	return false;
}

const char*
param_name_for_type (ARDOUR::AutomationType t)
{
	for (const ParamMap& m : kParamMap) {
		if (m.type == t) {
			return m.name;
		}
	}
	return 0;
}

bool
resolve_parameter (const std::string& name, Evoral::Parameter& out, std::string& error)
{
	for (const ParamMap& m : kParamMap) {
		if (name == m.name) {
			out = Evoral::Parameter (m.type, 0, 0);
			return true;
		}
	}
	error = std::string ("Unknown parameter \"") + name + "\" (try: gain, pan, mute, solo, rec_enable, rec_safe, trim, phase, monitoring)";
	return false;
}

std::string
handle_automation_list_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string route_id = root.get<std::string> ("params.arguments.id", "");
	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing route id (or pass \"selected\")");
	}

	std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}

	std::ostringstream ss;
	ss << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
	   << ",\"name\":\"" << json_escape (route->name ()) << "\""
	   << ",\"parameters\":[";

	bool first = true;
	for (const ParamMap& m : kParamMap) {
		const Evoral::Parameter p (m.type, 0, 0);
		std::shared_ptr<ARDOUR::AutomationControl> ctl = route->automation_control (p);
		if (!ctl) {
			continue;
		}
		if (!first) {
			ss << ",";
		}
		first = false;
		ss << "{\"name\":\"" << m.name << "\""
		   << ",\"value\":" << ctl->get_value ()
		   << ",\"state\":\"" << auto_state_string (ctl->automation_state ()) << "\""
		   << ",\"lower\":" << ctl->lower ()
		   << ",\"upper\":" << ctl->upper ()
		   << "}";
	}

	ss << "]}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Route automation listed\"}],\"structuredContent\":") + ss.str () + "}");
}

std::string
handle_automation_set_state_tool (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string route_id     = root.get<std::string> ("params.arguments.id", "");
	const std::string param_name   = root.get<std::string> ("params.arguments.parameter", "");
	const std::string state_string = root.get<std::string> ("params.arguments.state", "");

	if (route_id.empty ()) {
		return jsonrpc_error (id, -32602, "Missing route id");
	}
	if (param_name.empty ()) {
		return jsonrpc_error (id, -32602, "Missing parameter name");
	}
	if (state_string.empty ()) {
		return jsonrpc_error (id, -32602, "Missing state (expected: off, play, write, touch, latch)");
	}

	std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id_or_selection (session, route_id);
	if (!route) {
		return jsonrpc_error (id, -32602, "Route not found");
	}

	Evoral::Parameter param (ARDOUR::NullAutomation, 0, 0);
	std::string       err;
	if (!resolve_parameter (param_name, param, err)) {
		return jsonrpc_error (id, -32602, err);
	}

	ARDOUR::AutoState new_state;
	if (!parse_auto_state (state_string, new_state, err)) {
		return jsonrpc_error (id, -32602, err);
	}

	std::shared_ptr<ARDOUR::AutomationControl> ctl = route->automation_control (param);
	if (!ctl) {
		return jsonrpc_error (id, -32602, "Route has no such automation control");
	}

	const ARDOUR::AutoState previous = ctl->automation_state ();
	ctl->set_automation_state (new_state);

	std::ostringstream ss;
	ss << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
	   << ",\"parameter\":\"" << json_escape (param_name) << "\""
	   << ",\"previousState\":\"" << auto_state_string (previous) << "\""
	   << ",\"state\":\"" << auto_state_string (new_state) << "\""
	   << "}";
	return jsonrpc_result (
	    id,
	    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Automation state updated\"}],\"structuredContent\":") + ss.str () + "}");
}

} /* anonymous namespace */

bool
dispatch_automation_tool_call (ARDOUR::Session& session, const std::string& tool_name, const pt::ptree& root, const std::string& id, std::string& response)
{
	if (tool_name == "automation/list") {
		response = handle_automation_list_tool (session, root, id);
		return true;
	}
	if (tool_name == "automation/set_state") {
		response = handle_automation_set_state_tool (session, root, id);
		return true;
	}

	return false;
}

} /* namespace mcp */
} /* namespace ArdourSurface */

/*
 * Copyright (C) 2026 Frank Povazanj <frank.povazanj@gmail.com>
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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "pbd/controllable.h"
#include "pbd/event_loop.h"
#include "pbd/memento_command.h"

#include "ardour/amp.h"
#include "ardour/dB.h"
#include "ardour/plugin.h"
#include "ardour/plugin_insert.h"
#include "ardour/plugin_manager.h"
#include "ardour/processor.h"
#include "ardour/rc_configuration.h"
#include "ardour/route.h"
#include "ardour/session.h"
#include "ardour/types.h"

#include "handlers/common.h"
#include "handlers/plugins.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

class ScopedInstrumentPromptDisable
{
public:
	ScopedInstrumentPromptDisable ()
		: _config (ARDOUR::Config)
		, _ask_replace (false)
		, _ask_setup (false)
		, _active (false)
	{
		if (!_config) {
			return;
		}

		_ask_replace = _config->get_ask_replace_instrument ();
		_ask_setup   = _config->get_ask_setup_instrument ();
		_config->set_ask_replace_instrument (false);
		_config->set_ask_setup_instrument (false);
		_active = true;
	}

	~ScopedInstrumentPromptDisable ()
	{
		if (!_active || !_config) {
			return;
		}
		_config->set_ask_replace_instrument (_ask_replace);
		_config->set_ask_setup_instrument (_ask_setup);
	}

private:
	ARDOUR::RCConfiguration* _config;
	bool                     _ask_replace;
	bool                     _ask_setup;
	bool                     _active;
};

static int
plugin_index_for_processor (
    const std::shared_ptr<ARDOUR::Route>&     route,
    const std::shared_ptr<ARDOUR::Processor>& processor)
{
	if (!route || !processor) {
		return -1;
	}

	for (uint32_t i = 0;; ++i) {
		std::shared_ptr<ARDOUR::Processor> p = route->nth_plugin (i);
		if (!p) {
			break;
		}
		if (p == processor) {
			return (int)i;
		}
	}

	return -1;
}

static bool
wait_for_plugin_index (
    const std::shared_ptr<ARDOUR::Route>&     route,
    const std::shared_ptr<ARDOUR::Processor>& processor,
    int                                       target_index,
    int                                       timeout_ms,
    int&                                      resolved_index)
{
	const std::chrono::steady_clock::time_point deadline =
	    std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms);

	for (;;) {
		resolved_index = plugin_index_for_processor (route, processor);
		if (resolved_index == target_index) {
			return true;
		}
		if (std::chrono::steady_clock::now () >= deadline) {
			return false;
		}
		std::this_thread::sleep_for (std::chrono::milliseconds (2));
	}
}

static bool
wait_for_plugin_post_fader (
    const std::shared_ptr<ARDOUR::Route>&     route,
    const std::shared_ptr<ARDOUR::Processor>& processor,
    bool                                      target_post_fader,
    int                                       timeout_ms,
    int&                                      resolved_index,
    bool&                                     resolved_post_fader)
{
	const std::chrono::steady_clock::time_point deadline =
	    std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms);

	for (;;) {
		resolved_index      = plugin_index_for_processor (route, processor);
		resolved_post_fader = !processor->get_pre_fader ();
		if (resolved_index >= 0 && resolved_post_fader == target_post_fader) {
			return true;
		}
		if (std::chrono::steady_clock::now () >= deadline) {
			return false;
		}
		std::this_thread::sleep_for (std::chrono::milliseconds (2));
	}
}

static std::string
lower_ascii (std::string s)
{
	std::transform (
	    s.begin (),
	    s.end (),
	    s.begin (),
	    [] (unsigned char c) -> unsigned char { return (unsigned char)std::tolower (c); });
	return s;
}

static std::string
plugin_type_token (ARDOUR::PluginType type)
{
	switch (type) {
		case ARDOUR::AudioUnit:
			return "audiounit";
		case ARDOUR::LADSPA:
			return "ladspa";
		case ARDOUR::LV2:
			return "lv2";
		case ARDOUR::Windows_VST:
			return "windows_vst";
		case ARDOUR::LXVST:
			return "lxvst";
		case ARDOUR::MacVST:
			return "macvst";
		case ARDOUR::Lua:
			return "lua";
		case ARDOUR::VST3:
			return "vst3";
		default:
			return "unknown";
	}
}

static bool
parse_plugin_type_token (const std::string& value, ARDOUR::PluginType& type)
{
	const std::string token = lower_ascii (value);
	if (token == "audiounit" || token == "audio-unit" || token == "audio_unit" || token == "au") {
		type = ARDOUR::AudioUnit;
		return true;
	}
	if (token == "ladspa") {
		type = ARDOUR::LADSPA;
		return true;
	}
	if (token == "lv2") {
		type = ARDOUR::LV2;
		return true;
	}
	if (token == "windows_vst" || token == "windows_vst2" || token == "winvst" || token == "vst2_windows") {
		type = ARDOUR::Windows_VST;
		return true;
	}
	if (token == "lxvst" || token == "linux_vst" || token == "linux_vst2" || token == "vst2_linux") {
		type = ARDOUR::LXVST;
		return true;
	}
	if (token == "macvst" || token == "mac_vst" || token == "mac_vst2" || token == "vst2_mac") {
		type = ARDOUR::MacVST;
		return true;
	}
	if (token == "lua") {
		type = ARDOUR::Lua;
		return true;
	}
	if (token == "vst3") {
		type = ARDOUR::VST3;
		return true;
	}
	return false;
}

static std::string
plugin_status_string (ARDOUR::PluginManager::PluginStatusType status)
{
	switch (status) {
		case ARDOUR::PluginManager::Normal:
			return "normal";
		case ARDOUR::PluginManager::Favorite:
			return "favorite";
		case ARDOUR::PluginManager::Hidden:
			return "hidden";
		case ARDOUR::PluginManager::Concealed:
			return "concealed";
		default:
			return "unknown";
	}
}

static std::string
plugin_catalog_id (const ARDOUR::PluginInfoPtr& info)
{
	return plugin_type_token (info->type) + ":" + info->unique_id;
}

static bool
plugin_matches_search (const ARDOUR::PluginInfoPtr& info, const std::string& search_lower)
{
	if (search_lower.empty ()) {
		return true;
	}

	const std::string haystack = lower_ascii (
	    info->name + " " + info->creator + " " + info->category + " " + info->unique_id + " " + info->path);
	return haystack.find (search_lower) != std::string::npos;
}

static void
append_plugin_catalog_entries_json (
    std::ostringstream&                      out,
    const ARDOUR::PluginInfoList&            infos,
    ARDOUR::PluginManager&                   manager,
    const std::string&                       search_lower,
    const std::optional<ARDOUR::PluginType>& type_filter,
    bool                                     include_hidden,
    bool                                     include_internal,
    bool&                                    first_entry,
    size_t&                                  count)
{
	for (ARDOUR::PluginInfoList::const_iterator i = infos.begin (); i != infos.end (); ++i) {
		const ARDOUR::PluginInfoPtr& info = *i;
		if (!info) {
			continue;
		}

		if (type_filter && info->type != *type_filter) {
			continue;
		}
		if (!include_internal && info->is_internal ()) {
			continue;
		}

		const ARDOUR::PluginManager::PluginStatusType status = manager.get_status (info);
		const bool                                    hidden = status == ARDOUR::PluginManager::Hidden || status == ARDOUR::PluginManager::Concealed;
		if (!include_hidden && hidden) {
			continue;
		}
		if (!plugin_matches_search (info, search_lower)) {
			continue;
		}

		if (!first_entry) {
			out << ",";
		}
		first_entry = false;
		++count;

		out << "{\"pluginId\":\"" << json_escape (plugin_catalog_id (info)) << "\""
		    << ",\"type\":\"" << json_escape (plugin_type_token (info->type)) << "\""
		    << ",\"typeLabel\":\"" << json_escape (ARDOUR::PluginManager::plugin_type_name (info->type, false)) << "\""
		    << ",\"name\":\"" << json_escape (info->name) << "\""
		    << ",\"category\":\"" << json_escape (info->category) << "\""
		    << ",\"creator\":\"" << json_escape (info->creator) << "\""
		    << ",\"uniqueId\":\"" << json_escape (info->unique_id) << "\""
		    << ",\"path\":\"" << json_escape (info->path) << "\""
		    << ",\"status\":\"" << json_escape (plugin_status_string (status)) << "\""
		    << ",\"favorite\":" << (status == ARDOUR::PluginManager::Favorite ? "true" : "false")
		    << ",\"hidden\":" << (status == ARDOUR::PluginManager::Hidden ? "true" : "false")
		    << ",\"concealed\":" << (status == ARDOUR::PluginManager::Concealed ? "true" : "false")
		    << ",\"isInternal\":" << (info->is_internal () ? "true" : "false")
		    << ",\"isEffect\":" << (info->is_effect () ? "true" : "false")
		    << ",\"isInstrument\":" << (info->is_instrument () ? "true" : "false")
		    << ",\"isUtility\":" << (info->is_utility () ? "true" : "false")
		    << ",\"isAnalyzer\":" << (info->is_analyzer () ? "true" : "false")
		    << ",\"needsMidiInput\":" << (info->needs_midi_input () ? "true" : "false")
		    << ",\"nInputs\":{\"audio\":" << info->n_inputs.n_audio () << ",\"midi\":" << info->n_inputs.n_midi () << "}"
		    << ",\"nOutputs\":{\"audio\":" << info->n_outputs.n_audio () << ",\"midi\":" << info->n_outputs.n_midi () << "}"
		    << "}";
	}
}

static void
append_all_plugin_infos (std::vector<ARDOUR::PluginInfoPtr>& infos, const ARDOUR::PluginInfoList& list)
{
	for (ARDOUR::PluginInfoList::const_iterator i = list.begin (); i != list.end (); ++i) {
		if (*i) {
			infos.push_back (*i);
		}
	}
}

static bool
resolve_plugin_info_for_add (
    ARDOUR::PluginManager& manager,
    const std::string&     plugin_id,
    const std::string&     type_token,
    const std::string&     unique_id,
    ARDOUR::PluginInfoPtr& resolved,
    std::string&           error)
{
	resolved.reset ();
	error.clear ();

	std::optional<ARDOUR::PluginType> requested_type;
	std::string                       requested_unique_id;

	if (!plugin_id.empty ()) {
		const std::string::size_type sep = plugin_id.find (':');
		if (sep == std::string::npos || sep == 0 || sep + 1 >= plugin_id.size ()) {
			error = "Invalid pluginId (expected type:uniqueId)";
			return false;
		}

		ARDOUR::PluginType parsed_type;
		if (!parse_plugin_type_token (plugin_id.substr (0, sep), parsed_type)) {
			error = "Invalid pluginId type token";
			return false;
		}
		requested_type      = parsed_type;
		requested_unique_id = plugin_id.substr (sep + 1);
	} else {
		if (!type_token.empty ()) {
			ARDOUR::PluginType parsed_type;
			if (!parse_plugin_type_token (type_token, parsed_type)) {
				error = "Invalid plugin type";
				return false;
			}
			requested_type = parsed_type;
		}

		if (!unique_id.empty ()) {
			requested_unique_id = unique_id;
		}
	}

	if (requested_unique_id.empty ()) {
		error = "Missing plugin identifier (provide pluginId or uniqueId)";
		return false;
	}

	std::vector<ARDOUR::PluginInfoPtr> all;
	append_all_plugin_infos (all, manager.windows_vst_plugin_info ());
	append_all_plugin_infos (all, manager.lxvst_plugin_info ());
	append_all_plugin_infos (all, manager.mac_vst_plugin_info ());
	append_all_plugin_infos (all, manager.vst3_plugin_info ());
	append_all_plugin_infos (all, manager.au_plugin_info ());
	append_all_plugin_infos (all, manager.ladspa_plugin_info ());
	append_all_plugin_infos (all, manager.lv2_plugin_info ());
	append_all_plugin_infos (all, manager.lua_plugin_info ());

	size_t matches = 0;
	for (size_t i = 0; i < all.size (); ++i) {
		const ARDOUR::PluginInfoPtr& info = all[i];
		if (!info || info->unique_id != requested_unique_id) {
			continue;
		}
		if (requested_type && info->type != *requested_type) {
			continue;
		}
		resolved = info;
		++matches;
	}

	if (matches == 0) {
		error = "Plugin not found";
		return false;
	}
	if (matches > 1 && !requested_type) {
		error = "Ambiguous uniqueId across plugin types; provide plugin type";
		resolved.reset ();
		return false;
	}

	return true;
}

static std::string
variant_type_string (ARDOUR::Variant::Type type)
{
	switch (type) {
		case ARDOUR::Variant::BEATS:
			return "BEATS";
		case ARDOUR::Variant::BOOL:
			return "BOOL";
		case ARDOUR::Variant::DOUBLE:
			return "DOUBLE";
		case ARDOUR::Variant::FLOAT:
			return "FLOAT";
		case ARDOUR::Variant::INT:
			return "INT";
		case ARDOUR::Variant::LONG:
			return "LONG";
		case ARDOUR::Variant::NOTHING:
			return "NOTHING";
		case ARDOUR::Variant::PATH:
			return "PATH";
		case ARDOUR::Variant::STRING:
			return "STRING";
		case ARDOUR::Variant::URI:
			return "URI";
		default:
			return "UNKNOWN";
	}
}

static std::string
plugin_descriptor_json (const std::shared_ptr<ARDOUR::Route>& route, int plugin_index, std::string* error_message)
{
	std::shared_ptr<ARDOUR::Processor> proc = route->nth_plugin (plugin_index);
	if (!proc) {
		if (error_message) {
			*error_message = "Plugin not found";
		}
		return std::string ();
	}

	std::shared_ptr<ARDOUR::PluginInsert> pi = std::dynamic_pointer_cast<ARDOUR::PluginInsert> (proc);
	if (!pi) {
		if (error_message) {
			*error_message = "Processor is not a plugin";
		}
		return std::string ();
	}

	std::shared_ptr<ARDOUR::Plugin> pip = pi->plugin ();
	if (!pip) {
		if (error_message) {
			*error_message = "Plugin instance unavailable";
		}
		return std::string ();
	}

	std::ostringstream ss;
	ss << "{\"plugin\":{\"index\":" << plugin_index
	   << ",\"name\":\"" << json_escape (proc->name ()) << "\""
	   << ",\"displayName\":\"" << json_escape (proc->display_name ()) << "\""
	   << ",\"enabled\":" << (proc->enabled () ? "true" : "false")
	   << ",\"active\":" << (proc->active () ? "true" : "false")
	   << ",\"maker\":\"" << json_escape (pip->maker ()) << "\""
	   << ",\"label\":\"" << json_escape (pip->label ()) << "\""
	   << ",\"uniqueId\":\"" << json_escape (pip->unique_id ()) << "\""
	   << "},\"parameters\":[";

	bool ok    = false;
	bool first = true;
	for (uint32_t ppi = 0; ppi < pip->parameter_count (); ++ppi) {
		const uint32_t controlid = pip->nth_parameter (ppi, ok);
		if (!ok) {
			continue;
		}

		ARDOUR::ParameterDescriptor pd;
		if (pip->get_parameter_descriptor (controlid, pd) != 0) {
			continue;
		}

		if (!first) {
			ss << ",";
		}
		first = false;

		int flags = 0;
		flags |= pd.enumeration ? 1 : 0;
		flags |= pd.integer_step ? 2 : 0;
		flags |= pd.logarithmic ? 4 : 0;
		flags |= pd.sr_dependent ? 32 : 0;
		flags |= pd.toggled ? 64 : 0;
		flags |= pip->parameter_is_input (controlid) ? 0x80 : 0;
		std::string param_desc = pip->describe_parameter (Evoral::Parameter (ARDOUR::PluginAutomation, 0, controlid));
		flags |= (param_desc == "hidden") ? 0x100 : 0;

		ss << "{\"index\":" << ppi
		   << ",\"number\":" << (ppi + 1)
		   << ",\"controlId\":" << controlid
		   << ",\"label\":\"" << json_escape (pd.label) << "\""
		   << ",\"flags\":" << flags
		   << ",\"datatype\":\"" << variant_type_string (pd.datatype) << "\""
		   << ",\"lower\":" << pd.lower
		   << ",\"upper\":" << pd.upper
		   << ",\"printFmt\":\"" << json_escape (pd.print_fmt) << "\""
		   << ",\"isInput\":" << (pip->parameter_is_input (controlid) ? "true" : "false")
		   << ",\"isOutput\":" << (pip->parameter_is_output (controlid) ? "true" : "false")
		   << ",\"isControl\":" << (pip->parameter_is_control (controlid) ? "true" : "false")
		   << ",\"isHidden\":" << ((flags & 0x100) ? "true" : "false");

		ss << ",\"scalePoints\":[";
		bool first_scale = true;
		if (pd.scale_points) {
			for (ARDOUR::ScalePoints::const_iterator i = pd.scale_points->begin (); i != pd.scale_points->end (); ++i) {
				if (!first_scale) {
					ss << ",";
				}
				first_scale = false;
				ss << "{\"value\":" << i->second
				   << ",\"label\":\"" << json_escape (i->first) << "\"}";
			}
		}
		ss << "]";

		std::shared_ptr<ARDOUR::AutomationControl> c = pi->automation_control (Evoral::Parameter (ARDOUR::PluginAutomation, 0, controlid));
		if (c) {
			ss << ",\"currentValue\":" << c->get_value ()
			   << ",\"currentInterface\":" << c->internal_to_interface (c->get_value ());
		} else {
			ss << ",\"currentValue\":0"
			   << ",\"currentInterface\":0";
		}

		ss << "}";
	}

	ss << "]}";
	return ss.str ();
}

static std::string
handle_plugin_tool_call (ARDOUR::Session& session, PBD::EventLoop* event_loop, const std::string& tool_name, const pt::ptree& root, const std::string& id)
{
	ARDOUR::Session& _session    = session;
	PBD::EventLoop*  _event_loop = event_loop;

	if (tool_name == "plugin/list_available") {
		const std::string search           = root.get<std::string> ("params.arguments.search", "");
		const std::string type_token       = root.get<std::string> ("params.arguments.type", "");
		const bool        include_hidden   = root.get<bool> ("params.arguments.includeHidden", false);
		const bool        include_internal = root.get<bool> ("params.arguments.includeInternal", false);

		std::optional<ARDOUR::PluginType> type_filter;
		if (!type_token.empty ()) {
			ARDOUR::PluginType parsed_type;
			if (!parse_plugin_type_token (type_token, parsed_type)) {
				return jsonrpc_error (id, -32602, "Invalid type filter");
			}
			type_filter = parsed_type;
		}

		ARDOUR::PluginManager& manager      = ARDOUR::PluginManager::instance ();
		const std::string      search_lower = lower_ascii (search);

		std::ostringstream plugins;
		plugins << "[";
		bool   first = true;
		size_t count = 0;

		append_plugin_catalog_entries_json (
		    plugins,
		    manager.windows_vst_plugin_info (),
		    manager,
		    search_lower,
		    type_filter,
		    include_hidden,
		    include_internal,
		    first,
		    count);
		append_plugin_catalog_entries_json (
		    plugins,
		    manager.lxvst_plugin_info (),
		    manager,
		    search_lower,
		    type_filter,
		    include_hidden,
		    include_internal,
		    first,
		    count);
		append_plugin_catalog_entries_json (
		    plugins,
		    manager.mac_vst_plugin_info (),
		    manager,
		    search_lower,
		    type_filter,
		    include_hidden,
		    include_internal,
		    first,
		    count);
		append_plugin_catalog_entries_json (
		    plugins,
		    manager.vst3_plugin_info (),
		    manager,
		    search_lower,
		    type_filter,
		    include_hidden,
		    include_internal,
		    first,
		    count);
		append_plugin_catalog_entries_json (
		    plugins,
		    manager.au_plugin_info (),
		    manager,
		    search_lower,
		    type_filter,
		    include_hidden,
		    include_internal,
		    first,
		    count);
		append_plugin_catalog_entries_json (
		    plugins,
		    manager.ladspa_plugin_info (),
		    manager,
		    search_lower,
		    type_filter,
		    include_hidden,
		    include_internal,
		    first,
		    count);
		append_plugin_catalog_entries_json (
		    plugins,
		    manager.lv2_plugin_info (),
		    manager,
		    search_lower,
		    type_filter,
		    include_hidden,
		    include_internal,
		    first,
		    count);
		append_plugin_catalog_entries_json (
		    plugins,
		    manager.lua_plugin_info (),
		    manager,
		    search_lower,
		    type_filter,
		    include_hidden,
		    include_internal,
		    first,
		    count);
		plugins << "]";

		std::ostringstream structured;
		structured << "{\"count\":" << count
		           << ",\"filters\":{\"search\":\"" << json_escape (search) << "\"";
		if (type_filter) {
			structured << ",\"type\":\"" << json_escape (plugin_type_token (*type_filter)) << "\"";
		} else {
			structured << ",\"type\":null";
		}
		structured << ",\"includeHidden\":" << (include_hidden ? "true" : "false")
		           << ",\"includeInternal\":" << (include_internal ? "true" : "false")
		           << "},\"plugins\":" << plugins.str ()
		           << "}";

		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Available plugins listed\"}],\"structuredContent\":") + structured.str () + "}");
	}

	if (tool_name == "plugin/add") {
		const std::string            route_id     = root.get<std::string> ("params.arguments.id", "");
		const std::string            plugin_id    = root.get<std::string> ("params.arguments.pluginId", "");
		const std::string            unique_id    = root.get<std::string> ("params.arguments.uniqueId", "");
		const std::string            type_token   = root.get<std::string> ("params.arguments.type", "");
		const std::optional<int64_t> position_opt = get_optional<int64_t> (root, "params.arguments.position");
		const std::optional<bool>    enabled_opt  = get_optional<bool> (root, "params.arguments.enabled");

		if (route_id.empty ()) {
			return jsonrpc_error (id, -32602, "Missing route id");
		}
		if (plugin_id.empty () && unique_id.empty ()) {
			return jsonrpc_error (id, -32602, "Provide pluginId or uniqueId");
		}
		if (position_opt && *position_opt < 0) {
			return jsonrpc_error (id, -32602, "Invalid position (expected >= 0)");
		}

		const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id (_session, route_id);
		if (!route) {
			return jsonrpc_error (id, -32602, "Route not found");
		}

		ARDOUR::PluginManager& manager = ARDOUR::PluginManager::instance ();
		ARDOUR::PluginInfoPtr  info;
		std::string            resolve_error;
		if (!resolve_plugin_info_for_add (manager, plugin_id, type_token, unique_id, info, resolve_error)) {
			return jsonrpc_error (id, -32602, resolve_error.empty () ? "Could not resolve plugin" : resolve_error);
		}

		std::shared_ptr<ARDOUR::Processor> processor;
		std::string                        add_error;
		bool                               add_ok             = false;
		const bool                         activation_allowed = enabled_opt ? *enabled_opt : ARDOUR::Config->get_new_plugins_active ();

		auto perform_add = [&] () {
			const ARDOUR::PluginPtr plugin = info->load (_session);
			if (!plugin) {
				add_error = "Failed to load plugin";
				return;
			}

			processor.reset (new ARDOUR::PluginInsert (_session, *route, plugin));
			ARDOUR::Route::ProcessorStreams err;

			/* Suppress interactive instrument setup prompts so Route::PluginSetup does
			 * not invoke GUI dialog code while handling MCP requests.
			 */
			ScopedInstrumentPromptDisable suppress_prompts;

			int rc = 0;
			if (position_opt) {
				rc = route->add_processor (processor, route->before_processor_for_index ((int)*position_opt), &err, activation_allowed);
			} else {
				rc = route->add_processor (processor, ARDOUR::PreFader, &err, activation_allowed);
			}
			if (rc != 0) {
				add_error = "Failed to add plugin to route";
				processor.reset ();
				return;
			}

			/* Explicitly honor enabled=false requests after insertion. */
			if (enabled_opt && !*enabled_opt) {
				processor->enable (false);
			}

			add_ok = true;
		};

#ifdef __APPLE__
		/* Some macOS plugins (Qt/Cocoa) require construction on the main/UI event loop.
		 * Keep behavior unchanged on other platforms for now.
		 */
		if (_event_loop) {
			std::mutex              add_mutex;
			std::condition_variable add_cv;
			bool                    add_done = false;

			const bool queued = _event_loop->call_slot (MISSING_INVALIDATOR, [&] () {
				perform_add ();
				{
					std::lock_guard<std::mutex> lk (add_mutex);
					add_done = true;
				}
				add_cv.notify_one (); });

			if (queued) {
				std::unique_lock<std::mutex> lk (add_mutex);
				add_cv.wait (lk, [&] { return add_done; });
			} else {
				perform_add ();
			}
		} else {
			perform_add ();
		}
#else
		perform_add ();
#endif

		if (!add_ok || !processor) {
			return jsonrpc_error (id, -32000, add_error.empty () ? "Failed to add plugin to route" : add_error);
		}

		int inserted_index = -1;
		for (uint32_t i = 0;; ++i) {
			std::shared_ptr<ARDOUR::Processor> p = route->nth_plugin (i);
			if (!p) {
				break;
			}
			if (p == processor) {
				inserted_index = (int)i;
				break;
			}
		}

		std::ostringstream structured;
		structured << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
		           << ",\"routeName\":\"" << json_escape (route->name ()) << "\""
		           << ",\"pluginIndex\":" << inserted_index
		           << ",\"plugin\":{\"pluginId\":\"" << json_escape (plugin_catalog_id (info)) << "\""
		           << ",\"type\":\"" << json_escape (plugin_type_token (info->type)) << "\""
		           << ",\"typeLabel\":\"" << json_escape (ARDOUR::PluginManager::plugin_type_name (info->type, false)) << "\""
		           << ",\"name\":\"" << json_escape (info->name) << "\""
		           << ",\"uniqueId\":\"" << json_escape (info->unique_id) << "\""
		           << ",\"enabled\":" << (processor->enabled () ? "true" : "false")
		           << ",\"active\":" << (processor->active () ? "true" : "false")
		           << "}"
		           << ",\"plugins\":" << mcp::plugin_list_json (route)
		           << "}";

		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Plugin added\"}],\"structuredContent\":") + structured.str () + "}");
	}

	if (tool_name == "plugin/get_description") {
		const std::string route_id     = root.get<std::string> ("params.arguments.id", "");
		const int         plugin_index = root.get<int> ("params.arguments.pluginIndex", -1);

		if (route_id.empty ()) {
			return jsonrpc_error (id, -32602, "Missing route id");
		}
		if (plugin_index < 0) {
			return jsonrpc_error (id, -32602, "Invalid pluginIndex (expected >= 0)");
		}

		const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id (_session, route_id);
		if (!route) {
			return jsonrpc_error (id, -32602, "Route not found");
		}

		std::string error_message;
		std::string structured = plugin_descriptor_json (route, plugin_index, &error_message);
		if (structured.empty ()) {
			return jsonrpc_error (id, -32602, error_message.empty () ? "Could not describe plugin" : error_message);
		}

		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Plugin descriptor\"}],\"structuredContent\":") + structured + "}");
	}

	if (tool_name == "plugin/set_parameter") {
		const std::string           route_id        = root.get<std::string> ("params.arguments.id", "");
		const int                   plugin_index    = root.get<int> ("params.arguments.pluginIndex", -1);
		const std::optional<int>    parameter_index = get_optional<int> (root, "params.arguments.parameterIndex");
		const std::optional<int>    control_id      = get_optional<int> (root, "params.arguments.controlId");
		const std::optional<double> value           = get_optional<double> (root, "params.arguments.value");
		const std::optional<double> interface_value = get_optional<double> (root, "params.arguments.interface");

		if (route_id.empty ()) {
			return jsonrpc_error (id, -32602, "Missing route id");
		}
		if (plugin_index < 0) {
			return jsonrpc_error (id, -32602, "Invalid pluginIndex (expected >= 0)");
		}
		if (!parameter_index && !control_id) {
			return jsonrpc_error (id, -32602, "Provide one of: parameterIndex or controlId");
		}
		if (parameter_index && control_id) {
			return jsonrpc_error (id, -32602, "Provide only one of: parameterIndex or controlId");
		}
		if (!value && !interface_value) {
			return jsonrpc_error (id, -32602, "Provide one of: value or interface");
		}
		if (value && interface_value) {
			return jsonrpc_error (id, -32602, "Provide only one of: value or interface");
		}
		if (parameter_index && *parameter_index < 0) {
			return jsonrpc_error (id, -32602, "Invalid parameterIndex (expected >= 0)");
		}
		if (control_id && *control_id < 0) {
			return jsonrpc_error (id, -32602, "Invalid controlId (expected >= 0)");
		}

		const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id (_session, route_id);
		if (!route) {
			return jsonrpc_error (id, -32602, "Route not found");
		}

		std::shared_ptr<ARDOUR::Processor> proc = route->nth_plugin (plugin_index);
		if (!proc) {
			return jsonrpc_error (id, -32602, "Plugin not found");
		}

		std::shared_ptr<ARDOUR::PluginInsert> pi = std::dynamic_pointer_cast<ARDOUR::PluginInsert> (proc);
		if (!pi) {
			return jsonrpc_error (id, -32602, "Processor is not a plugin");
		}

		std::shared_ptr<ARDOUR::Plugin> pip = pi->plugin ();
		if (!pip) {
			return jsonrpc_error (id, -32602, "Plugin instance unavailable");
		}

		bool     ok                       = false;
		uint32_t resolved_control_id      = 0;
		int      resolved_parameter_index = -1;

		if (parameter_index) {
			resolved_control_id = pip->nth_parameter ((uint32_t)*parameter_index, ok);
			if (!ok) {
				return jsonrpc_error (id, -32602, "parameterIndex out of range");
			}
			resolved_parameter_index = *parameter_index;
		} else {
			resolved_control_id = (uint32_t)*control_id;
			for (uint32_t ppi = 0; ppi < pip->parameter_count (); ++ppi) {
				const uint32_t cid = pip->nth_parameter (ppi, ok);
				if (!ok) {
					continue;
				}
				if (cid == resolved_control_id) {
					resolved_parameter_index = (int)ppi;
					break;
				}
			}
			if (resolved_parameter_index < 0) {
				return jsonrpc_error (id, -32602, "controlId not found");
			}
		}

		ARDOUR::ParameterDescriptor pd;
		if (pip->get_parameter_descriptor (resolved_control_id, pd) != 0) {
			return jsonrpc_error (id, -32602, "Could not read parameter descriptor");
		}
		if (!(pip->parameter_is_input (resolved_control_id) || pip->parameter_is_control (resolved_control_id))) {
			return jsonrpc_error (id, -32602, "Parameter is not writable");
		}

		std::shared_ptr<ARDOUR::AutomationControl> c =
		    pi->automation_control (Evoral::Parameter (ARDOUR::PluginAutomation, 0, resolved_control_id));
		if (!c) {
			return jsonrpc_error (id, -32602, "Parameter automation control not available");
		}

		double new_value = c->get_value ();
		if (interface_value) {
			if (!std::isfinite (*interface_value)) {
				return jsonrpc_error (id, -32602, "Invalid interface value");
			}
			/* Match OSC plugin-parameter flow: convert interface value through the automation control. */
			new_value = c->interface_to_internal (*interface_value);
		} else {
			if (!std::isfinite (*value)) {
				return jsonrpc_error (id, -32602, "Invalid parameter value");
			}
			new_value = *value;
		}
		if (!std::isfinite (new_value)) {
			return jsonrpc_error (id, -32602, "Parameter mapping produced invalid value");
		}

		/* Clamp to control bounds to match defensive handling used elsewhere in MCP tools. */
		new_value = std::max (c->lower (), std::min (c->upper (), new_value));
		c->set_value (new_value, PBD::Controllable::NoGroup);

		const double current_value     = c->get_value ();
		const double current_interface = c->internal_to_interface (current_value);

		std::ostringstream structured;
		structured << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
		           << ",\"routeName\":\"" << json_escape (route->name ()) << "\""
		           << ",\"pluginIndex\":" << plugin_index
		           << ",\"pluginName\":\"" << json_escape (proc->name ()) << "\""
		           << ",\"parameterIndex\":" << resolved_parameter_index
		           << ",\"controlId\":" << resolved_control_id
		           << ",\"label\":\"" << json_escape (pd.label) << "\""
		           << ",\"value\":" << current_value
		           << ",\"interface\":" << current_interface
		           << "}";

		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Plugin parameter updated\"}],\"structuredContent\":") + structured.str () + "}");
	}

	if (tool_name == "plugin/set_enabled") {
		const std::string         route_id     = root.get<std::string> ("params.arguments.id", "");
		const int                 plugin_index = root.get<int> ("params.arguments.pluginIndex", -1);
		const std::optional<bool> enabled      = get_optional<bool> (root, "params.arguments.enabled");

		if (route_id.empty ()) {
			return jsonrpc_error (id, -32602, "Missing route id");
		}
		if (plugin_index < 0) {
			return jsonrpc_error (id, -32602, "Invalid pluginIndex (expected >= 0)");
		}
		if (!enabled) {
			return jsonrpc_error (id, -32602, "Missing enabled boolean");
		}

		const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id (_session, route_id);
		if (!route) {
			return jsonrpc_error (id, -32602, "Route not found");
		}

		std::shared_ptr<ARDOUR::Processor> proc = route->nth_plugin (plugin_index);
		if (!proc) {
			return jsonrpc_error (id, -32602, "Plugin not found");
		}

		std::shared_ptr<ARDOUR::PluginInsert> pi = std::dynamic_pointer_cast<ARDOUR::PluginInsert> (proc);
		if (!pi) {
			return jsonrpc_error (id, -32602, "Processor is not a plugin");
		}

		/* Match OSC route_plugin_activate/deactivate behavior. */
		if (*enabled) {
			pi->activate ();
		} else {
			pi->deactivate ();
		}

		std::ostringstream structured;
		structured << "{\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
		           << ",\"routeName\":\"" << json_escape (route->name ()) << "\""
		           << ",\"pluginIndex\":" << plugin_index
		           << ",\"pluginName\":\"" << json_escape (proc->name ()) << "\""
		           << ",\"requestedEnabled\":" << (*enabled ? "true" : "false")
		           << ",\"enabled\":" << (proc->enabled () ? "true" : "false")
		           << ",\"active\":" << (proc->active () ? "true" : "false")
		           << "}";

		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Plugin enabled state updated\"}],\"structuredContent\":") + structured.str () + "}");
	}

	if (tool_name == "plugin/remove") {
		const std::string route_id     = root.get<std::string> ("params.arguments.id", "");
		const int         plugin_index = root.get<int> ("params.arguments.pluginIndex", -1);

		if (route_id.empty ()) {
			return jsonrpc_error (id, -32602, "Missing route id");
		}
		if (plugin_index < 0) {
			return jsonrpc_error (id, -32602, "Invalid pluginIndex (expected >= 0)");
		}

		const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id (_session, route_id);
		if (!route) {
			return jsonrpc_error (id, -32602, "Route not found");
		}

		std::shared_ptr<ARDOUR::Processor> proc = route->nth_plugin (plugin_index);
		if (!proc) {
			return jsonrpc_error (id, -32602, "Plugin not found");
		}

		const std::string removed_name    = proc->name ();
		const bool        removed_enabled = proc->enabled ();
		const bool        removed_active  = proc->active ();
		if (route->remove_processor (proc) != 0) {
			return jsonrpc_error (id, -32000, "Failed to remove plugin");
		}

		std::ostringstream structured;
		structured << "{\"removed\":true"
		           << ",\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
		           << ",\"routeName\":\"" << json_escape (route->name ()) << "\""
		           << ",\"pluginIndex\":" << plugin_index
		           << ",\"removedPlugin\":{\"name\":\"" << json_escape (removed_name) << "\""
		           << ",\"enabled\":" << (removed_enabled ? "true" : "false")
		           << ",\"active\":" << (removed_active ? "true" : "false")
		           << "}"
		           << ",\"plugins\":" << mcp::plugin_list_json (route)
		           << "}";

		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Plugin removed\"}],\"structuredContent\":") + structured.str () + "}");
	}

	if (tool_name == "plugin/reorder") {
		const std::string route_id   = root.get<std::string> ("params.arguments.id", "");
		const int         from_index = root.get<int> ("params.arguments.fromIndex", -1);
		const int         to_index   = root.get<int> ("params.arguments.toIndex", -1);

		if (route_id.empty ()) {
			return jsonrpc_error (id, -32602, "Missing route id");
		}
		if (from_index < 0 || to_index < 0) {
			return jsonrpc_error (id, -32602, "Invalid fromIndex/toIndex (expected >= 0)");
		}

		const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id (_session, route_id);
		if (!route) {
			return jsonrpc_error (id, -32602, "Route not found");
		}

		std::vector<std::shared_ptr<ARDOUR::Processor>> all_processors;
		route->foreach_processor ([&all_processors] (std::weak_ptr<ARDOUR::Processor> wp) {
			std::shared_ptr<ARDOUR::Processor> p = wp.lock ();
			if (p) {
				all_processors.push_back (p);
			} });

		std::vector<std::shared_ptr<ARDOUR::Processor>> plugins;
		for (size_t i = 0; i < all_processors.size (); ++i) {
			if (std::dynamic_pointer_cast<ARDOUR::PluginInsert> (all_processors[i])) {
				plugins.push_back (all_processors[i]);
			}
		}

		if (from_index >= (int)plugins.size () || to_index >= (int)plugins.size ()) {
			return jsonrpc_error (id, -32602, "Plugin index out of range");
		}

		if (from_index == to_index) {
			std::ostringstream structured;
			structured << "{\"moved\":false"
			           << ",\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
			           << ",\"routeName\":\"" << json_escape (route->name ()) << "\""
			           << ",\"fromIndex\":" << from_index
			           << ",\"toIndex\":" << to_index
			           << ",\"plugins\":" << mcp::plugin_list_json (route)
			           << "}";
			return jsonrpc_result (
			    id,
			    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Plugin order unchanged\"}],\"structuredContent\":") + structured.str () + "}");
		}

		const std::shared_ptr<ARDOUR::Processor> moved_plugin = plugins[(size_t)from_index];
		const std::string                        moved_name   = moved_plugin->name ();
		plugins.erase (plugins.begin () + from_index);
		plugins.insert (plugins.begin () + to_index, moved_plugin);

		ARDOUR::Route::ProcessorList reordered_all;
		size_t                       plugin_cursor = 0;
		for (size_t i = 0; i < all_processors.size (); ++i) {
			if (std::dynamic_pointer_cast<ARDOUR::PluginInsert> (all_processors[i])) {
				reordered_all.push_back (plugins[plugin_cursor++]);
			} else {
				reordered_all.push_back (all_processors[i]);
			}
		}

		if (route->reorder_processors (reordered_all) != 0) {
			return jsonrpc_error (id, -32000, "Failed to reorder plugins");
		}

		int        resolved_to_index = -1;
		const bool settled           = wait_for_plugin_index (route, moved_plugin, to_index, 500, resolved_to_index);
		const bool moved             = resolved_to_index >= 0 && resolved_to_index != from_index;
		const bool reached_target    = resolved_to_index == to_index;

		std::ostringstream structured;
		structured << "{\"moved\":" << (moved ? "true" : "false")
		           << ",\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
		           << ",\"routeName\":\"" << json_escape (route->name ()) << "\""
		           << ",\"pluginName\":\"" << json_escape (moved_name) << "\""
		           << ",\"fromIndex\":" << from_index
		           << ",\"toIndex\":" << to_index
		           << ",\"settled\":" << (settled ? "true" : "false")
		           << ",\"reachedTarget\":" << (reached_target ? "true" : "false")
		           << ",\"resolvedToIndex\":" << resolved_to_index
		           << ",\"plugins\":" << mcp::plugin_list_json (route)
		           << "}";

		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + (reached_target ? "Plugin order updated" : (moved ? "Plugin order changed" : "Plugin order unchanged")) + "\"}],\"structuredContent\":" + structured.str () + "}");
	}

	if (tool_name == "plugin/set_position") {
		const std::string         route_id       = root.get<std::string> ("params.arguments.id", "");
		const int                 plugin_index   = root.get<int> ("params.arguments.pluginIndex", -1);
		const std::optional<bool> post_fader_opt = get_optional<bool> (root, "params.arguments.postFader");

		if (route_id.empty ()) {
			return jsonrpc_error (id, -32602, "Missing route id");
		}
		if (plugin_index < 0) {
			return jsonrpc_error (id, -32602, "Invalid pluginIndex (expected >= 0)");
		}
		if (!post_fader_opt) {
			return jsonrpc_error (id, -32602, "Missing postFader boolean");
		}

		const std::shared_ptr<ARDOUR::Route> route = route_by_mcp_id (_session, route_id);
		if (!route) {
			return jsonrpc_error (id, -32602, "Route not found");
		}

		const std::shared_ptr<ARDOUR::Processor> moved_plugin = route->nth_plugin ((uint32_t)plugin_index);
		if (!moved_plugin) {
			return jsonrpc_error (id, -32602, "Plugin not found");
		}

		const bool requested_post_fader = *post_fader_opt;
		const bool current_post_fader   = !moved_plugin->get_pre_fader ();
		if (requested_post_fader == current_post_fader) {
			std::ostringstream structured;
			structured << "{\"moved\":false"
			           << ",\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
			           << ",\"routeName\":\"" << json_escape (route->name ()) << "\""
			           << ",\"pluginIndex\":" << plugin_index
			           << ",\"pluginName\":\"" << json_escape (moved_plugin->name ()) << "\""
			           << ",\"preFader\":" << (moved_plugin->get_pre_fader () ? "true" : "false")
			           << ",\"postFader\":" << (moved_plugin->get_pre_fader () ? "false" : "true")
			           << ",\"plugins\":" << mcp::plugin_list_json (route)
			           << "}";
			return jsonrpc_result (
			    id,
			    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"Plugin placement unchanged\"}],\"structuredContent\":") + structured.str () + "}");
		}

		std::vector<std::shared_ptr<ARDOUR::Processor>> all_processors;
		route->foreach_processor ([&all_processors] (std::weak_ptr<ARDOUR::Processor> wp) {
			std::shared_ptr<ARDOUR::Processor> p = wp.lock ();
			if (p) {
				all_processors.push_back (p);
			} });

		std::vector<std::shared_ptr<ARDOUR::Processor>>::iterator it =
		    std::find (all_processors.begin (), all_processors.end (), moved_plugin);
		if (it == all_processors.end ()) {
			return jsonrpc_error (id, -32000, "Plugin disappeared before reorder");
		}
		all_processors.erase (it);

		size_t insert_pos = all_processors.size ();
		if (requested_post_fader) {
			const std::shared_ptr<ARDOUR::Processor> main_outs = route->main_outs ();
			if (main_outs) {
				for (size_t i = 0; i < all_processors.size (); ++i) {
					if (all_processors[i] == main_outs) {
						insert_pos = i;
						break;
					}
				}
			}
		} else {
			insert_pos                                   = 0;
			const std::shared_ptr<ARDOUR::Processor> amp = route->amp ();
			if (amp) {
				for (size_t i = 0; i < all_processors.size (); ++i) {
					if (all_processors[i] == amp) {
						insert_pos = i;
						break;
					}
				}
			}
		}
		all_processors.insert (all_processors.begin () + insert_pos, moved_plugin);

		ARDOUR::Route::ProcessorList reordered_all;
		for (size_t i = 0; i < all_processors.size (); ++i) {
			reordered_all.push_back (all_processors[i]);
		}

		if (route->reorder_processors (reordered_all) != 0) {
			return jsonrpc_error (id, -32000, "Failed to update plugin placement");
		}

		int        resolved_index      = -1;
		bool       resolved_post_fader = !moved_plugin->get_pre_fader ();
		const bool settled             = wait_for_plugin_post_fader (
                    route,
                    moved_plugin,
                    requested_post_fader,
                    500,
                    resolved_index,
                    resolved_post_fader);
		const bool moved          = (resolved_post_fader != current_post_fader);
		const bool reached_target = (resolved_post_fader == requested_post_fader);

		std::ostringstream structured;
		structured << "{\"moved\":" << (moved ? "true" : "false")
		           << ",\"id\":\"" << json_escape (route->id ().to_s ()) << "\""
		           << ",\"routeName\":\"" << json_escape (route->name ()) << "\""
		           << ",\"pluginIndex\":" << plugin_index
		           << ",\"resolvedPluginIndex\":" << resolved_index
		           << ",\"pluginName\":\"" << json_escape (moved_plugin->name ()) << "\""
		           << ",\"requestedPostFader\":" << (requested_post_fader ? "true" : "false")
		           << ",\"settled\":" << (settled ? "true" : "false")
		           << ",\"reachedTarget\":" << (reached_target ? "true" : "false")
		           << ",\"preFader\":" << (resolved_post_fader ? "false" : "true")
		           << ",\"postFader\":" << (resolved_post_fader ? "true" : "false")
		           << ",\"plugins\":" << mcp::plugin_list_json (route)
		           << "}";

		return jsonrpc_result (
		    id,
		    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + (reached_target ? "Plugin placement updated" : (moved ? "Plugin placement changed" : "Plugin placement unchanged")) + "\"}],\"structuredContent\":" + structured.str () + "}");
	}

	return std::string ();
}

} /* anonymous namespace */

bool
dispatch_plugin_tool_call (ARDOUR::Session& session, PBD::EventLoop* event_loop, const std::string& tool_name, const pt::ptree& root, const std::string& id, std::string& response)
{
	response = handle_plugin_tool_call (session, event_loop, tool_name, root, id);
	return !response.empty ();
}

} /* namespace mcp */
} /* namespace ArdourSurface */

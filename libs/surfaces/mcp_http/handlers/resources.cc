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
#include <sstream>
#include <string>

#include "pbd/xml++.h"

#include "ardour/location.h"
#include "ardour/route.h"
#include "ardour/selection.h"
#include "ardour/session.h"
#include "ardour/stripable.h"
#include "ardour/tempo.h"

#include "handlers/common.h"
#include "handlers/resources.h"

namespace ArdourSurface
{
namespace mcp
{
namespace
{

const char* const URI_SESSION_CONTEXT = "ardour://session/context.json";

std::string
context_json (ARDOUR::Session& session)
{
	Temporal::TempoMap::fetch ();

	std::ostringstream ss;
	ss << "{\"sessionName\":\"" << json_escape (session.name ()) << "\""
	   << ",\"snapshotName\":\"" << json_escape (session.snap_name ()) << "\""
	   << ",\"sampleRate\":" << session.nominal_sample_rate ()
	   << ",\"tempoBpm\":" << transport_tempo_bpm (session)
	   << ",\"transport\":" << transport_state_json (session)
	   << ",\"playhead\":{\"sample\":" << session.transport_sample ()
	   << ",\"bbt\":" << bbt_json_at_sample (session.transport_sample ())
	   << "}";

	/* Selected route, if any. */
	const std::shared_ptr<ARDOUR::Route> sel = std::dynamic_pointer_cast<ARDOUR::Route> (session.selection ().first_selected_stripable ());
	if (sel) {
		ss << ",\"selectedRoute\":{\"id\":\"" << json_escape (sel->id ().to_s ()) << "\""
		   << ",\"name\":\"" << json_escape (sel->name ()) << "\""
		   << ",\"type\":\"" << route_type_string (sel) << "\""
		   << "}";
	} else {
		ss << ",\"selectedRoute\":null";
	}

	/* Nearest marker before/at the playhead. */
	const samplepos_t now = session.transport_sample ();
	ARDOUR::Locations* locations = session.locations ();
	ARDOUR::Location*  nearest   = 0;
	if (locations) {
		for (auto loc : locations->list ()) {
			if (!loc || !(loc->is_mark () || loc->is_range_marker ())) {
				continue;
			}
			if (loc->start_sample () <= now) {
				if (!nearest || loc->start_sample () > nearest->start_sample ()) {
					nearest = loc;
				}
			}
		}
	}
	if (nearest) {
		ss << ",\"nearestMarker\":{\"id\":\"" << json_escape (nearest->id ().to_s ()) << "\""
		   << ",\"name\":\"" << json_escape (nearest->name ()) << "\""
		   << ",\"startSample\":" << nearest->start_sample ()
		   << ",\"bbt\":" << bbt_json_at_sample (nearest->start_sample ())
		   << "}";
	} else {
		ss << ",\"nearestMarker\":null";
	}

	ss << "}";
	return ss.str ();
}

std::string
resource_entry_json (const char* uri, const char* name, const char* description, const char* mime)
{
	std::ostringstream ss;
	ss << "{\"uri\":\"" << uri << "\""
	   << ",\"name\":\"" << name << "\""
	   << ",\"description\":\"" << description << "\""
	   << ",\"mimeType\":\"" << mime << "\""
	   << "}";
	return ss.str ();
}

} /* anonymous namespace */

std::string
handle_resources_list (ARDOUR::Session& /*session*/, const std::string& id)
{
	std::ostringstream ss;
	ss << "{\"resources\":["
	   << resource_entry_json (URI_SESSION_CONTEXT, "Session context",
	                           "Compact, model-friendly summary of the session right now: tempo at the playhead, selected route, nearest marker, transport state. Read this before deciding how to act.",
	                           "application/json")
	   << "]}";
	return jsonrpc_result (id, ss.str ());
}

std::string
handle_resources_read (ARDOUR::Session& session, const pt::ptree& root, const std::string& id)
{
	const std::string uri = root.get<std::string> ("params.uri", "");
	if (uri.empty ()) {
		return jsonrpc_error (id, -32602, "Missing uri");
	}

	std::string text;
	std::string mime;

	if (uri == URI_SESSION_CONTEXT) {
		text = context_json (session);
		mime = "application/json";
	} else {
		return jsonrpc_error (id, -32602, std::string ("Unknown resource uri: ") + uri);
	}

	std::ostringstream ss;
	ss << "{\"contents\":[{\"uri\":\"" << json_escape (uri) << "\""
	   << ",\"mimeType\":\"" << mime << "\""
	   << ",\"text\":\"" << json_escape (text) << "\""
	   << "}]}";
	return jsonrpc_result (id, ss.str ());
}

} /* namespace mcp */
} /* namespace ArdourSurface */

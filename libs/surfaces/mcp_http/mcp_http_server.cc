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
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "pbd/basename.h"
#include "pbd/controllable.h"
#include "pbd/enumwriter.h"
#include "pbd/error.h"
#include "pbd/event_loop.h"
#include "pbd/id.h"
#include "pbd/memento_command.h"
#include "pbd/pthread_utils.h"
#include "pbd/stateful_diff_command.h"
#include "pbd/xml++.h"

#include "ardour/amp.h"
#include "ardour/audio_track.h"
#include "ardour/audioregion.h"
#include "ardour/dB.h"
#include "ardour/internal_send.h"
#include "ardour/location.h"
#include "ardour/midi_model.h"
#include "ardour/midi_region.h"
#include "ardour/midi_source.h"
#include "ardour/midi_track.h"
#include "ardour/playlist.h"
#include "ardour/plugin.h"
#include "ardour/plugin_insert.h"
#include "ardour/plugin_manager.h"
#include "ardour/presentation_info.h"
#include "ardour/processor.h"
#include "ardour/rc_configuration.h"
#include "ardour/region.h"
#include "ardour/region_factory.h"
#include "ardour/route.h"
#include "ardour/selection.h"
#include "ardour/session.h"
#include "ardour/session_event.h"
#include "ardour/source.h"
#include "ardour/stripable.h"
#include "ardour/tempo.h"
#include "ardour/track.h"

#include "handlers/common.h"
#include "handlers/markers.h"
#include "handlers/plugins.h"
#include "handlers/regions_midi.h"
#include "handlers/session.h"
#include "handlers/track.h"
#include "handlers/tracks.h"
#include "handlers/transport.h"
#include "mcp_http_server.h"

namespace pt = boost::property_tree;

using namespace ArdourSurface;


MCPHttpServer::MCPHttpServer (ARDOUR::Session& session, uint16_t port, int debug_level, PBD::EventLoop* event_loop)
	: _session (session)
	, _port (port)
	, _debug_level (std::clamp (debug_level, 0, 2))
	, _event_loop (event_loop)
	, _context (0)
	, _running (false)
{
	memset (_protocols, 0, sizeof (_protocols));
	memset (&_info, 0, sizeof (_info));
}

MCPHttpServer::~MCPHttpServer ()
{
	stop ();
}

int
MCPHttpServer::start ()
{
	if (_context) {
		return 0;
	}

	_protocols[0].name                  = "mcp-http";
	_protocols[0].callback              = MCPHttpServer::lws_callback;
	_protocols[0].per_session_data_size = 0;
	_protocols[0].rx_buffer_size        = 0;
	_protocols[0].id                    = 0;
	_protocols[0].user                  = 0;
#if LWS_LIBRARY_VERSION_MAJOR >= 3
	_protocols[0].tx_packet_size = 0;
#endif

	_info.port      = _port;
	_info.protocols = _protocols;
	_info.gid       = -1;
	_info.uid       = -1;
	_info.user      = this;

	_context = lws_create_context (&_info);
	if (!_context) {
		PBD::error << "MCPHttp: could not create libwebsockets context" << endmsg;
		return -1;
	}

	_running        = true;
	_service_thread = std::thread (&MCPHttpServer::run, this);

	return 0;
}

int
MCPHttpServer::stop ()
{
	if (!_context) {
		return 0;
	}

	_running = false;
	lws_cancel_service (_context);

	if (_service_thread.joinable ()) {
		_service_thread.join ();
	}

	lws_context_destroy (_context);
	_context = 0;
	_clients.clear ();

	return 0;
}

void
MCPHttpServer::set_debug_level (int level)
{
	_debug_level.store (std::clamp (level, 0, 2));
}

int
MCPHttpServer::debug_level () const
{
	return _debug_level.load ();
}

void
MCPHttpServer::run ()
{
	if (_event_loop) {
		PBD::EventLoop::set_event_loop_for_thread (_event_loop);
	}

	PBD::notify_event_loops_about_thread_creation (pthread_self (), "MCPHttp", 2048);

	/* Session transport requests allocate SessionEvent objects from a per-thread pool. */
	ARDOUR::SessionEvent::create_per_thread_pool ("MCPHttp events", 256);
	Temporal::TempoMap::fetch ();

	while (_running) {
		lws_service (_context, 100);
	}
}

MCPHttpServer::ClientContext&
MCPHttpServer::client (struct lws* wsi)
{
	ClientMap::iterator it = _clients.find (wsi);
	if (it == _clients.end ()) {
		ClientContext ctx;
		ctx.mcp_post      = false;
		ctx.have_response = false;
		it                = _clients.emplace (wsi, ctx).first;
	}

	return it->second;
}

void
MCPHttpServer::erase_client (struct lws* wsi)
{
	ClientMap::iterator it = _clients.find (wsi);
	if (it != _clients.end ()) {
		_clients.erase (it);
	}
}

int
MCPHttpServer::handle_http (struct lws* wsi, ClientContext& ctx)
{
	ctx.mcp_post      = false;
	ctx.have_response = false;
	ctx.request_body.clear ();
	ctx.response_body.clear ();

	char        uri[1024];
	std::string path;

	if (lws_hdr_copy (wsi, uri, sizeof (uri), WSI_TOKEN_GET_URI) > 0) {
		path = uri;

		/* HTTP-only MCP endpoint: POST /mcp */
		if (path == "/mcp") {
			return send_http_status (wsi, 405);
		}

		return send_http_status (wsi, 404);
	}

	if (lws_hdr_copy (wsi, uri, sizeof (uri), WSI_TOKEN_POST_URI) > 0) {
		path = uri;

		if (path == "/mcp") {
			ctx.mcp_post = true;
			return 0;
		}

		return send_http_status (wsi, 404);
	}

	return send_http_status (wsi, 400);
}

int
MCPHttpServer::handle_http_body (struct lws* /*wsi*/, ClientContext& ctx, void* in, size_t len)
{
	if (!ctx.mcp_post) {
		return 0;
	}

	ctx.request_body.append (static_cast<char*> (in), len);
	return 0;
}

int
MCPHttpServer::handle_http_body_completion (struct lws* wsi, ClientContext& ctx)
{
	if (!ctx.mcp_post) {
		return send_http_status (wsi, 400);
	}

	ctx.response_body = dispatch_jsonrpc (ctx.request_body);

	if (ctx.response_body.empty ()) {
		return send_http_status (wsi, 202);
	}

	ctx.have_response = true;

	if (send_json_headers (wsi)) {
		return 1;
	}

	lws_callback_on_writable (wsi);
	return 0;
}

int
MCPHttpServer::handle_http_writeable (struct lws* wsi, ClientContext& ctx)
{
	if (ctx.have_response) {
		return write_json_response (wsi, ctx);
	}

	return 0;
}

int
MCPHttpServer::send_http_status (struct lws* wsi, unsigned int status)
{
	lws_return_http_status (wsi, status, 0);
	return -1;
}

int
MCPHttpServer::send_json_headers (struct lws* wsi)
{
	unsigned char  out_buf[1024];
	unsigned char* start = out_buf;
	unsigned char* p     = start;
	unsigned char* end   = &out_buf[sizeof (out_buf) - 1];

#if LWS_LIBRARY_VERSION_MAJOR >= 3
	if (lws_add_http_common_headers (wsi, 200, "application/json", LWS_ILLEGAL_HTTP_CONTENT_LEN, &p, end) || lws_add_http_header_by_token (wsi, WSI_TOKEN_HTTP_CACHE_CONTROL, reinterpret_cast<const unsigned char*> ("no-store"), 8, &p, end) || lws_finalize_write_http_header (wsi, start, &p, end)) {
		return 1;
	}
#else
	if (lws_add_http_header_status (wsi, 200, &p, end) || lws_add_http_header_by_token (wsi, WSI_TOKEN_HTTP_CONTENT_TYPE, reinterpret_cast<const unsigned char*> ("application/json"), 16, &p, end) || lws_add_http_header_by_token (wsi, WSI_TOKEN_CONNECTION, reinterpret_cast<const unsigned char*> ("close"), 5, &p, end) || lws_add_http_header_by_token (wsi, WSI_TOKEN_HTTP_CACHE_CONTROL, reinterpret_cast<const unsigned char*> ("no-store"), 8, &p, end) || lws_finalize_http_header (wsi, &p, end)) {
		return 1;
	}

	int len = p - start;
	if (lws_write (wsi, start, len, LWS_WRITE_HTTP_HEADERS) != len) {
		return 1;
	}
#endif

	return 0;
}

int
MCPHttpServer::write_json_response (struct lws* wsi, ClientContext& ctx)
{
	std::vector<unsigned char> body (ctx.response_body.begin (), ctx.response_body.end ());
	if (!body.empty () && lws_write (wsi, body.data (), body.size (), LWS_WRITE_HTTP) != (int)body.size ()) {
		return 1;
	}

	ctx.have_response = false;
	ctx.mcp_post      = false;
	ctx.request_body.clear ();
	ctx.response_body.clear ();

	if (lws_http_transaction_completed (wsi)) {
		return -1;
	}

	return -1;
}





std::string
MCPHttpServer::dispatch_jsonrpc (const std::string& payload) const
{
	pt::ptree          root;
	std::istringstream is (payload);
	const int          dbg = debug_level ();

	try {
		pt::read_json (is, root);
	} catch (...) {
		if (dbg >= 1) {
			PBD::warning << "MCPHttp: JSON parse error" << endmsg;
		}
		return mcp::jsonrpc_error ("null", -32700, "Parse error");
	}

	std::string method = root.get<std::string> ("method", "");
	std::string id     = mcp::jsonrpc_id (root);
	const bool  has_id = mcp::has_jsonrpc_id (root);

	if (dbg >= 2) {
		PBD::info << "MCPHttp: request payload: " << payload << endmsg;
	}
	if (dbg >= 1) {
		if (method == "tools/call") {
			const std::string requested_tool = mcp::canonical_tool_name (root.get<std::string> ("params.name", ""));
			PBD::info << "MCPHttp: tools/call " << requested_tool << endmsg;
		} else {
			PBD::info << "MCPHttp: " << method << endmsg;
		}
	}

	if (method.empty ()) {
		return mcp::jsonrpc_error (id, -32600, "Invalid Request");
	}

	if (method == "initialize") {
		return mcp::jsonrpc_result (
		    id,
		    "{\"protocolVersion\":\"2025-03-26\","
		    "\"capabilities\":{\"tools\":{\"listChanged\":false}},"
		    "\"serverInfo\":{\"name\":\"ardour-mcp-http\",\"version\":\"0.1.0\"}}");
	}

	if (method == "notifications/initialized" && !has_id) {
		/* JSON-RPC notification: no response body. */
		return std::string ();
	}

	if (method == "ping" || method == "notifications/initialized") {
		return mcp::jsonrpc_result (id, "{}");
	}

	if (method == "tools/list") {
		static constexpr std::string_view tools_list{
#include "tools_json.inc"
		};

		return mcp::jsonrpc_result (
		    id,
		    std::string (tools_list));
	}

	if (method == "tools/call") {
		std::string tool_name = mcp::canonical_tool_name (root.get<std::string> ("params.name", ""));
		if (tool_name == "hello_world") {
			std::string caller = root.get<std::string> ("params.arguments.name", "");
			std::string text   = "Hello from Ardour";
			if (!caller.empty ()) {
				text += ", " + caller;
			}
			text += " (session: " + _session.name () + ")";

			return mcp::jsonrpc_result (
			    id,
			    std::string ("{\"content\":[{\"type\":\"text\",\"text\":\"") + mcp::json_escape (text) + "\"}]}");
		}

		std::string track_tool_response;
		if (mcp::dispatch_track_tool_call (_session, tool_name, root, id, track_tool_response)) {
			return track_tool_response;
		}

		std::string session_tool_response;
		if (mcp::dispatch_session_tool_call (_session, tool_name, root, id, session_tool_response)) {
			return session_tool_response;
		}

		std::string transport_tool_response;
		if (mcp::dispatch_transport_tool_call (_session, tool_name, root, id, transport_tool_response)) {
			return transport_tool_response;
		}

		std::string markers_tool_response;
		if (mcp::dispatch_markers_tool_call (_session, tool_name, root, id, markers_tool_response)) {
			return markers_tool_response;
		}

		std::string tracks_tool_response;
		if (mcp::dispatch_tracks_tool_call (_session, tool_name, root, id, tracks_tool_response)) {
			return tracks_tool_response;
		}

		std::string plugin_tool_response;
		if (mcp::dispatch_plugin_tool_call (_session, _event_loop, tool_name, root, id, plugin_tool_response)) {
			return plugin_tool_response;
		}

		std::string midi_region_tool_response;
		if (mcp::dispatch_regions_midi_tool_call (_session, tool_name, root, id, midi_region_tool_response)) {
			return midi_region_tool_response;
		}

		return mcp::jsonrpc_error (id, -32602, "Unknown tool name");
	}

	return mcp::jsonrpc_error (id, -32601, "Method not found");
}

int
MCPHttpServer::callback (struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
{
	ClientContext& ctx = client (wsi);
	int            rc  = 0;

	switch (reason) {
		case LWS_CALLBACK_HTTP:
			rc = handle_http (wsi, ctx);
			break;
		case LWS_CALLBACK_HTTP_BODY:
			rc = handle_http_body (wsi, ctx, in, len);
			break;
		case LWS_CALLBACK_HTTP_BODY_COMPLETION:
			rc = handle_http_body_completion (wsi, ctx);
			break;
		case LWS_CALLBACK_HTTP_WRITEABLE:
			rc = handle_http_writeable (wsi, ctx);
			break;
		case LWS_CALLBACK_CLOSED:
#ifdef LWS_CALLBACK_CLOSED_HTTP
		case LWS_CALLBACK_CLOSED_HTTP:
#endif
#ifdef LWS_CALLBACK_WSI_DESTROY
		case LWS_CALLBACK_WSI_DESTROY:
#endif
			erase_client (wsi);
			rc = 0;
			break;
#if ((LWS_LIBRARY_VERSION_MAJOR * 1000000) + (LWS_LIBRARY_VERSION_MINOR * 1000)) >= 2001000
		default:
			rc = lws_callback_http_dummy (wsi, reason, user, in, len);
			break;
#else
		default:
			rc = 0;
			break;
#endif
	}

	return rc;
}

int
MCPHttpServer::lws_callback (struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
{
	void*          ctx_userdata = lws_context_user (lws_get_context (wsi));
	MCPHttpServer* server       = static_cast<MCPHttpServer*> (ctx_userdata);
	if (!server) {
		return 0;
	}

	return server->callback (wsi, reason, user, in, len);
}

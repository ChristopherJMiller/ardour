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

#ifndef _ardour_surface_mcp_http_server_h_
#define _ardour_surface_mcp_http_server_h_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <string>
#include <thread>
#include <unordered_map>

#include <libwebsockets.h>

#include "pbd/signals.h"

namespace PBD
{
class EventLoop;
}

namespace ARDOUR
{
class Session;
}

namespace ArdourSurface
{

class MCPHttpServer
{
public:
	MCPHttpServer (ARDOUR::Session&, uint16_t port, int debug_level, PBD::EventLoop* event_loop);
	~MCPHttpServer ();

	int  start ();
	int  stop ();
	void set_debug_level (int);
	int  debug_level () const;

public:
	enum SubscriptionBit {
		SubTransport = 1 << 0,
		SubSelection = 1 << 1,
		SubRoutes    = 1 << 2,
		SubMarkers   = 1 << 3,
		SubRecord    = 1 << 4,
	};

private:
	struct ClientContext {
		bool        mcp_post;
		bool        sse;
		bool        sse_headers_sent;
		bool        have_response;
		uint32_t    subscriptions;
		std::string request_body;
		std::string response_body;
		std::deque<std::string> sse_queue;
		/* per-client mutex for sse_queue */
		std::shared_ptr<std::mutex> sse_mutex;
	};

	typedef std::unordered_map<struct lws*, ClientContext> ClientMap;

	ARDOUR::Session&                 _session;
	uint16_t                         _port;
	std::atomic<int>                 _debug_level;
	PBD::EventLoop*                  _event_loop;
	struct lws_context*              _context;
	struct lws_protocols             _protocols[2];
	struct lws_context_creation_info _info;
	ClientMap                        _clients;
	std::mutex                       _clients_mutex;
	std::thread                      _service_thread;
	bool                             _running;

	/* SSE plumbing */
	PBD::ScopedConnectionList _sse_signal_connections;
	std::atomic<int>          _sse_client_count;
	std::mutex                _position_coalesce_mutex;
	std::chrono::steady_clock::time_point _last_position_emit;

	void run ();

	ClientContext& client (struct lws*);
	void           erase_client (struct lws*);

	int callback (struct lws*, enum lws_callback_reasons, void*, void*, size_t);
	int handle_http (struct lws*, ClientContext&);
	int handle_http_body (struct lws*, ClientContext&, void*, size_t);
	int handle_http_body_completion (struct lws*, ClientContext&);
	int handle_http_writeable (struct lws*, ClientContext&);

	int send_json_headers (struct lws*);
	int send_http_status (struct lws*, unsigned int);
	int write_json_response (struct lws*, ClientContext&);

	int begin_sse (struct lws*, ClientContext&, uint32_t subscriptions);
	int write_sse_headers (struct lws*);
	int write_sse_frames (struct lws*, ClientContext&);
	uint32_t parse_subscriptions (const std::string& query);

	void setup_session_signals ();
	void teardown_session_signals ();
	void emit_sse_event (uint32_t needed_subscription, const std::string& event_name, const std::string& json_payload);

	std::string dispatch_jsonrpc (const std::string&) const;

	static int lws_callback (struct lws*, enum lws_callback_reasons, void*, void*, size_t);
};

} // namespace ArdourSurface

#endif // _ardour_surface_mcp_http_server_h_

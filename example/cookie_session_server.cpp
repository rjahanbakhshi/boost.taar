//
// Copyright (c) 2021-2026 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost.taar
//
// Cookie-keyed sessions. A context_store holds one user_session per key,
// expiring idle ones; with_session reads the key from a cookie, creates a
// session (and sets the cookie) or requires one, locks it for the duration
// of the request, and injects it into the handler.
//
//   ./cookie_session_server 8080
//   curl -c jar -b jar localhost:8080/                      # visits: 1
//   curl -c jar -b jar localhost:8080/                      # visits: 2
//   curl -c jar -b jar -d 'user=ada&password=secret' localhost:8080/login
//   curl -c jar -b jar localhost:8080/me
//   curl localhost:8080/me                                  # 401: no session cookie
//

#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/context_store.hpp>
#include <boost/taar/core/form_kvp.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/handler/session_key.hpp>
#include <boost/taar/handler/with_session.hpp>
#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/json/value.hpp>
#include <chrono>
#include <iostream>
#include <optional>
#include <string>

namespace {

struct user_session
{
    std::optional<std::string> user;
    int visits = 0;
};

} // namespace

int main(int argc, char* argv[])
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::handler::rest;
    using taar::handler::url_encoded_form_data_arg;
    using taar::handler::with_session;
    using taar::handler::session_policy;
    namespace session_key = taar::handler::session_key;

    char const* port = argc > 1 ? argv[1] : "8080";

    taar::context_store<std::string, user_session> sessions {std::chrono::minutes {30}};
    session_key::cookie const sid {"sid"};

    taar::session::http session;

    session.register_request_handler(
        method == http::verb::get && target == "/",
        with_session(sessions, sid, session_policy::create_if_missing,
            rest([](user_session& s)
            {
                ++s.visits;
                return boost::json::value {{"visits", s.visits}, {"user", s.user ? boost::json::value {*s.user} : nullptr}};
            })));

    session.register_request_handler(
        method == http::verb::post && target == "/login",
        with_session(sessions, sid, session_policy::create_if_missing,
            rest([](user_session& s, taar::form_kvp form)
            {
                if (form["password"] != "secret") throw std::runtime_error {"bad credentials"};
                s.user = form["user"];
                return boost::json::value {{"logged_in", *s.user}};
            },
            url_encoded_form_data_arg {})));

    // require: no cookie or an unknown/expired key answers 401 before the handler runs.
    session.register_request_handler(
        method == http::verb::get && target == "/me",
        with_session(sessions, sid, session_policy::require,
            rest([](user_session const& s)
            {
                return boost::json::value {{"user", s.user ? boost::json::value {*s.user} : nullptr}, {"visits", s.visits}};
            })));

    session.register_request_handler(
        method == http::verb::post && target == "/logout",
        with_session(sessions, sid, session_policy::require,
            rest([](user_session& s) { s.user.reset(); return "bye"; })));

    // A streaming handler under with_session: the entry is locked only
    // while the handler produces the stream, then the chunks flow unlocked.
    session.register_request_handler(
        method == http::verb::get && target == "/visits/stream",
        with_session(sessions, sid, session_policy::require,
            rest([](user_session const& s) -> taar::chunked_response<std::string>
            {
                co_yield taar::set_header(http::field::content_type, "text/event-stream");
                for (int i = 1; i <= s.visits; ++i)
                {
                    co_yield "data: visit " + std::to_string(i) + "\n\n";
                }
            })));

    net::io_context io;
    taar::cancellation_signals signals;
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto) { signals.emit(); });

    net::co_spawn(
        io,
        taar::server::tcp("0.0.0.0", port, session, signals,
            [](net::ip::tcp::endpoint const& ep) { std::clog << "Listening on " << ep << '\n'; }),
        taar::ignore_and_rethrow);

    io.run();
}

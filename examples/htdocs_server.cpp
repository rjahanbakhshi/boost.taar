//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//
// Static file server for a bundled single-page application. The document
// root is laid out as a front-end build tool emits it:
//
//   <root>/index.html                  the application shell
//   <root>/static/app.3f9a1c2e.js      content-hashed assets
//   <root>/static/app.3f9a1c2e.js.br   optional precompressed siblings
//   <root>/static/app.3f9a1c2e.js.gz
//   <root>/static/downloads/...        files offered as downloads
//
// /static/{*path} serves the assets: hashed names are cached for a year as
// immutable, precompressed siblings are chosen by Accept-Encoding, ranges
// and conditional requests work as usual. Everything else is the
// application: existing files are served, and paths without an extension
// (client-side routes such as /settings/profile) fall back to index.html.
//
//   ./htdocs_server 8080 ./dist
//   curl -i localhost:8080/settings/profile                 # index.html, no-cache
//   curl -i -H 'Accept-Encoding: br' localhost:8080/static/app.3f9a1c2e.js
//   curl -i -H 'Range: bytes=0-99' localhost:8080/static/downloads/manual.pdf
//

#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/handler/htdocs.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

int main(int argc, char* argv[])
{
    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using taar::matcher::method;
    using taar::matcher::target;

    if (argc != 3)
    {
        std::cerr << "Usage: htdocs_server <port> <document-root>\n";
        return 1;
    }
    std::filesystem::path const root {argv[2]};

    // Assets: long-lived caching for hashed names, precompressed variants.
    taar::handler::htdocs_options assets;
    assets.root = root / "static";
    assets.cache_rules = taar::handler::cache_rules::immutable_hashed();
    assets.default_cache_control = "public, max-age=3600";
    assets.precompressed = {"br", "gzip"};
    assets.on_response = [](std::string_view path, http::response_header<>& header)
    {
        if (path.starts_with("downloads/"))
        {
            header.set(http::field::content_disposition, "attachment");
        }
    };

    // The application: index.html for client-side routes, revalidated on every use.
    taar::handler::htdocs_options app;
    app.root = root;
    app.fallback_document = "index.html";
    app.cache_rules = taar::handler::cache_rules::immutable_hashed();

    taar::session::http session;
    session.set_response_filter([](http::request_header<> const&, http::response_header<>& header)
    {
        header.set(http::field::server, "taar-htdocs");
    });
    session.register_request_handler(
        method == http::verb::get && target == "/static/{*path}",
        taar::handler::htdocs {std::move(assets)});
    session.register_request_handler(
        method == http::verb::get && target == "/{*path}",
        taar::handler::htdocs {std::move(app)});

    net::io_context io;
    taar::cancellation_signals signals;
    net::signal_set os_signals {io, SIGINT, SIGTERM};
    os_signals.async_wait([&](auto, auto) { signals.emit(); });

    net::co_spawn(
        io,
        taar::server::tcp("0.0.0.0", argv[1], session, signals,
            [](net::ip::tcp::endpoint const& ep) { std::clog << "Listening on " << ep << '\n'; }),
        taar::ignore_and_rethrow);

    // File I/O is blocking and runs on these threads: size the pool for the disk.
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([&] { io.run(); });
    for (auto& t : threads) t.join();
}

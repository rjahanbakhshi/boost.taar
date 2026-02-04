//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//

#include <boost/taar/core/chunked_response.hpp>
#include <boost/taar/core/chunked_response_meta.hpp>
#include <boost/taar/core/response_builder.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/json/value.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <iostream>
#include <cstdlib>
#include <stdexcept>

namespace {

std::string_view content_type_from_extension(std::filesystem::path const& path)
{
    auto ext = path.extension().string();
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".pdf") return "application/pdf";
    if (ext == ".txt") return "text/plain";
    return "application/octet-stream";
}

// Helper generator for file header (demonstrates chaining)
boost::taar::chunked_response<std::string> file_header(std::string const& filename)
{
    co_yield std::string{"=== Begin: " + filename + " ===\n"};
}

// Helper generator for file footer (demonstrates chaining)
boost::taar::chunked_response<std::string> file_footer(std::string const& filename)
{
    co_yield std::string{"\n=== End: " + filename + " ===\n"};
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: chunked_server port /example/document/root\n";
        return EXIT_FAILURE;
    }

    namespace net = boost::asio;
    namespace http = boost::beast::http;
    namespace taar = boost::taar;
    using net::co_spawn;
    using net::bind_cancellation_slot;
    using net::detached;
    using taar::response_builder;
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::matcher::context;

    net::io_context io_context;

    net::signal_set os_signals(io_context, SIGINT, SIGTERM);
    os_signals.async_wait([&](auto, auto) { io_context.stop(); });

    taar::session::http http_session;

    http_session.set_soft_error_handler(
        [](std::exception_ptr eptr)
        {
            try
            {
                std::rethrow_exception(eptr);
            }
            catch (std::exception const& e)
            {
                std::cerr << e.what() << '\n';
                return
                    response_builder(boost::json::value{{"error", e.what()}})
                        .set_status(http::status::not_found);
            }
            catch (...)
            {
                std::cerr << "Unknown error!\n";
                return
                    response_builder(boost::json::value{{"error", "Unknown error!"}})
                        .set_status(http::status::internal_server_error);
            }
        }
    );

    std::string doc_root = argv[2];

    // Endpoint demonstrating generator chaining - wraps file with header/footer
    http_session.register_request_handler(
        method == http::verb::get && target == "/wrapped/{*path}",
        [doc_root](
            http::request<http::empty_body> const&,
            context const& ctx) -> taar::chunked_response<std::string>
        {
            auto path = std::filesystem::path{doc_root} / ctx.path_args.at("path");
            std::ifstream file{path, std::ios::binary};
            if (!file)
                throw std::runtime_error("File not found: " + path.string());

            auto filename = path.filename().string();

            // Set content-type to text for wrapped output
            co_yield taar::set_header(http::field::content_type, "text/plain");

            // Chain the header generator - yields its values inline
            co_yield file_header(filename);

            // Yield file content
            char buffer[4096];
            while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
            {
                co_yield std::string{buffer, static_cast<std::size_t>(file.gcount())};
            }

            // Chain the footer generator - yields its values inline
            co_yield file_footer(filename);
        }
    );

    http_session.register_request_handler(
        method == http::verb::get && target == "/{*path}",
        [doc_root](
            http::request<http::empty_body> const&,
            context const& context) -> taar::chunked_response<std::string>
        {
            auto path = std::filesystem::path{doc_root} / context.path_args.at("path");
            std::ifstream file{path, std::ios::binary};
            if (!file)
                throw std::runtime_error("File not found: " + path.string());

            // Set content-type based on file extension
            co_yield taar::set_header(
                http::field::content_type,
                std::string{content_type_from_extension(path)});

            char buffer[4096];
            while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
            {
                co_yield std::string{buffer, static_cast<std::size_t>(file.gcount())};
            }
        }
    );

    taar::cancellation_signals cancellation_signals;
    co_spawn(
        io_context,
        taar::server::tcp(
            "0.0.0.0",
            argv[1],
            http_session,
            cancellation_signals,
            [](net::ip::tcp::endpoint const& endpoint)
            {
                std::clog << "HTTP server is listening on port " << endpoint.port() << '\n';
            }),
        bind_cancellation_slot(cancellation_signals.slot(), taar::ignore_and_rethrow));

    std::vector<std::jthread> threads;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&]{io_context.run();});
    }

    return EXIT_SUCCESS;
}

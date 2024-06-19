//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-web
//

#ifndef BOOST_WEB_HANDLER_HTDOCS_HPP
#define BOOST_WEB_HANDLER_HTDOCS_HPP

#include <boost/web/matcher/context.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/file_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <filesystem>
#include <format>
#include <string_view>
#include <string>
#include <utility>

namespace boost::web::handler {

class htdocs
{
    using request_body_type = boost::beast::http::empty_body;
    using request_type = boost::beast::http::request<request_body_type>;

public:
    htdocs(
        std::string htdocs_root = "./",
        std::string default_doc = "index.html")
        : htdocs_root_ {std::move(htdocs_root)}
        , default_doc_ {std::move(default_doc)}
    {}

    boost::beast::http::message_generator operator()(
        const request_type& request,
        const boost::web::matcher::context& context) const
    {
        namespace fs = std::filesystem;
        namespace http = boost::beast::http;

        try
        {
            // Make sure we can handle the method
            if (request.method() != http::verb::get &&
                request.method() != http::verb::head)
            {
                return text_response(
                    request,
                    http::status::bad_request,
                    "Unknown HTTP-method");
            }

            // Request path must be absolute and not contain "..".
            if (request.target().empty() ||
                request.target()[0] != '/' ||
                request.target().find("..") != std::string_view::npos)
            {
                return text_response(
                    request,
                    http::status::bad_request,
                    "Illegal request-target");
            }

            // Build the path to the requested file
            auto doc_path =
                fs::path{htdocs_root_} /
                fs::path{request.target().substr(1)};
            if(request.target().back() == '/')
            {
                doc_path /= default_doc_;
            }

            // Attempt to open the file
            boost::system::error_code ec;
            http::file_body::value_type body;
            body.open(doc_path.c_str(), boost::beast::file_mode::scan, ec);

            // Handle the case where the file doesn't exist
            if (ec == boost::beast::errc::no_such_file_or_directory)
            {
                return text_response(
                    request,
                    http::status::not_found,
                    std::format(
                        "The resource '{}' was not found.",
                        std::string_view{request.target()}));
            }

            // Unknown error
            if (ec)
            {
                return text_response(
                    request,
                    http::status::internal_server_error,
                    std::format("An error occurred: '{}'", ec.message()));
            }

            auto const content_length = body.size();

            // Respond to HEAD request
            if (request.method() == http::verb::head)
            {
                    http::response<http::empty_body> response {
                        http::status::ok,
                        request.version()};
                    response.set(http::field::content_type, mime_type(doc_path.c_str()));
                    response.content_length(content_length);
                    response.keep_alive(request.keep_alive());
                    return response;
            }

            // Respond to GET request
            http::response<http::file_body> response {
                    std::piecewise_construct,
                    std::make_tuple(std::move(body)),
                    std::make_tuple(http::status::ok, request.version())};
            response.set(http::field::content_type, mime_type(doc_path.c_str()));
            response.content_length(content_length);
            response.keep_alive(request.keep_alive());
            return response;
        }
        catch(const std::exception& e)
        {
            return text_response(
                request,
                http::status::bad_request,
                e.what());
        }
        catch(...)
        {
            return text_response(
                request,
                http::status::bad_request,
                "Unknown error");
        }
    }

private:
    static boost::beast::http::message_generator text_response(
        const request_type& request,
        boost::beast::http::status status,
        std::string message)
    {
        namespace http = boost::beast::http;
        http::response<http::string_body> response {
            http::status::bad_request,
            request.version()};

        response.set(http::field::content_type, "text/html");
        response.keep_alive(request.keep_alive());
        response.body() = std::move(message);
        response.prepare_payload();
        return response;
    }

    static std::string_view mime_type(std::string_view path)
    {
        using boost::beast::iequals;

        auto const pos = path.rfind('.');
        auto extension =
            (pos == std::string_view::npos) ? std::string_view{} : path.substr(pos);

        if (iequals(extension, ".htm"))  return "text/html";
        if (iequals(extension, ".html")) return "text/html";
        if (iequals(extension, ".php"))  return "text/html";
        if (iequals(extension, ".css"))  return "text/css";
        if (iequals(extension, ".txt"))  return "text/plain";
        if (iequals(extension, ".js"))   return "application/javascript";
        if (iequals(extension, ".json")) return "application/json";
        if (iequals(extension, ".xml"))  return "application/xml";
        if (iequals(extension, ".swf"))  return "application/x-shockwave-flash";
        if (iequals(extension, ".flv"))  return "video/x-flv";
        if (iequals(extension, ".png"))  return "image/png";
        if (iequals(extension, ".jpe"))  return "image/jpeg";
        if (iequals(extension, ".jpeg")) return "image/jpeg";
        if (iequals(extension, ".jpg"))  return "image/jpeg";
        if (iequals(extension, ".gif"))  return "image/gif";
        if (iequals(extension, ".bmp"))  return "image/bmp";
        if (iequals(extension, ".ico"))  return "image/vnd.microsoft.icon";
        if (iequals(extension, ".tiff")) return "image/tiff";
        if (iequals(extension, ".tif"))  return "image/tiff";
        if (iequals(extension, ".svg"))  return "image/svg+xml";
        if (iequals(extension, ".svgz")) return "image/svg+xml";

        return "application/text";
    }

private:
    std::string htdocs_root_;
    std::string default_doc_;
};

} // namespace boost::web::handler

#endif // BOOST_WEB_HANDLER_HTDOCS_HPP

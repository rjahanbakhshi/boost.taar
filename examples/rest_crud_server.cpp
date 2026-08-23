//
// Copyright (c) 2022-2024 Reza Jahanbakhshi (reza dot jahanbakhshi at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/rjahanbakhshi/boost-taar
//
// A JSON REST API with CRUD routes, typed request bodies, a domain error
// mapped to HTTP status codes, and an OpenAPI 3.1 document generated from
// the routes themselves.
//
//   ./rest_crud_server 8080
//   curl -X POST localhost:8080/todos -H 'content-type: application/json' -d '{"title":"write docs"}'
//   curl 'localhost:8080/todos?page=1&per_page=10'
//   curl localhost:8080/todos/1
//   curl -X PUT localhost:8080/todos/1 -H 'content-type: application/json' -d '{"title":"write docs","done":true}'
//   curl -X DELETE localhost:8080/todos/1
//   curl -F list=@todos.txt -F done=true localhost:8080/todos/import   # one todo per line
//   curl localhost:8080/openapi.json
//

#include <boost/taar/core/cancellation_signals.hpp>
#include <boost/taar/core/ignore_and_rethrow.hpp>
#include <boost/taar/core/response_builder.hpp>
#include <boost/taar/handler/rest.hpp>
#include <boost/taar/handler/rest_arg.hpp>
#include <boost/taar/matcher/method.hpp>
#include <boost/taar/matcher/target.hpp>
#include <boost/taar/openapi/generator.hpp>
#include <boost/taar/server/tcp.hpp>
#include <boost/taar/session/http.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/describe.hpp>
#include <boost/json.hpp>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

namespace http = boost::beast::http;
namespace json = boost::json;
namespace taar = boost::taar;

struct todo
{
    int id;
    std::string title;
    bool done;
};
BOOST_DESCRIBE_STRUCT(todo, (), (id, title, done))

// value_to<todo_input> fills std::optional members with nullopt when the
// JSON omits them; every other described member is required.
struct todo_input
{
    std::string title;
    std::optional<bool> done;
};
BOOST_DESCRIBE_STRUCT(todo_input, (), (title, done))

struct not_found : std::runtime_error
{
    using runtime_error::runtime_error;
};

class todo_store
{
public:
    json::value list(int page, int per_page)
    {
        std::lock_guard const g {mutex_};
        json::array items;
        int skip = (page - 1) * per_page;
        for (auto const& [id, t] : items_)
        {
            if (skip-- > 0) continue;
            if (static_cast<int>(items.size()) == per_page) break;
            items.push_back(json::value_from(t));
        }
        return {{"page", page}, {"total", items_.size()}, {"items", std::move(items)}};
    }

    json::value get(int id)
    {
        std::lock_guard const g {mutex_};
        return json::value_from(find(id));
    }

    json::value create(todo_input in)
    {
        std::lock_guard const g {mutex_};
        todo t {next_id_++, std::move(in.title), in.done.value_or(false)};
        items_[t.id] = t;
        return json::value_from(t);
    }

    json::value update(int id, todo_input in)
    {
        std::lock_guard const g {mutex_};
        auto& t = find(id);
        t.title = std::move(in.title);
        t.done = in.done.value_or(false);
        return json::value_from(t);
    }

    void remove(int id)
    {
        std::lock_guard const g {mutex_};
        if (items_.erase(id) == 0) throw not_found {"todo " + std::to_string(id) + " not found"};
    }

private:
    todo& find(int id)
    {
        auto it = items_.find(id);
        if (it == items_.end()) throw not_found {"todo " + std::to_string(id) + " not found"};
        return it->second;
    }

    std::mutex mutex_;
    std::map<int, todo> items_;
    int next_id_ = 1;
};

// Turns exceptions from handlers into JSON error responses. Library
// argument errors (bad path/query/body) become 400, the domain's not_found
// becomes 404, anything else 500.
taar::response_builder on_soft_error(std::exception_ptr e)
{
    auto reply = [](http::status status, std::string message)
    {
        return taar::response_builder(json::value {{"error", std::move(message)}}).set_status(status);
    };
    try
    {
        std::rethrow_exception(e);
    }
    catch (not_found const& x)
    {
        return reply(http::status::not_found, x.what());
    }
    catch (boost::system::system_error const& x)
    {
        if (x.code().category() == taar::error_category())
        {
            return reply(http::status::bad_request, x.code().message());
        }
        return reply(http::status::internal_server_error, x.what());
    }
    catch (std::exception const& x)
    {
        return reply(http::status::internal_server_error, x.what());
    }
}

} // namespace

int main(int argc, char* argv[])
{
    namespace net = boost::asio;
    using taar::matcher::method;
    using taar::matcher::target;
    using taar::handler::rest;
    using taar::handler::path_arg;
    using taar::handler::query_arg;
    using taar::handler::json_body_arg;
    using taar::handler::multipart_form_arg;
    using taar::handler::with_default;
    using taar::handler::constrained;
    using taar::handler::description;

    char const* port = argc > 1 ? argv[1] : "8080";

    todo_store store;
    taar::openapi::generator openapi {{.title = "Todos", .version = "1.0.0", .description = "A tiny todo list"}};
    taar::session::http session;
    session.set_soft_error_handler(&on_soft_error);

    session.register_request_handler(
        method == http::verb::get && target == "/todos",
        rest([&](int page, int per_page) { return store.list(page, per_page); },
            constrained(with_default(query_arg("page", "1-based page number"), 1), {.minimum = 1}),
            constrained(with_default(query_arg("per_page", "Items per page"), 20), {.minimum = 1, .maximum = 100})),
        openapi.describe({.summary = "List todos", .tags = {"todos"}}));

    session.register_request_handler(
        method == http::verb::get && target == "/todos/{id}",
        rest([&](int id) { return store.get(id); }, path_arg("id", "Todo id")),
        openapi.describe({.summary = "Fetch one todo", .tags = {"todos"}, .responses = {{"200", "The todo"}, {"404", "No such todo"}}}));

    session.register_request_handler(
        method == http::verb::post && target == "/todos",
        rest([&](todo_input in) { return store.create(std::move(in)); },
            json_body_arg(description {"Todo to create: {title, done?}"})),
        openapi.describe({.summary = "Create a todo", .tags = {"todos"}}));

    session.register_request_handler(
        method == http::verb::put && target == "/todos/{id}",
        rest([&](int id, todo_input in) { return store.update(id, std::move(in)); },
            path_arg("id", "Todo id"),
            json_body_arg(description {"New title and done flag"})),
        openapi.describe({.summary = "Replace a todo", .tags = {"todos"}}));

    session.register_request_handler(
        method == http::verb::delete_ && target == "/todos/{id}",
        rest([&](int id) { store.remove(id); }, path_arg("id", "Todo id")),
        openapi.describe({.summary = "Delete a todo", .tags = {"todos"}}));

    // A multipart form: text fields plus an uploaded file, buffered under the body limit.
    session.register_request_handler(
        method == http::verb::post && target == "/todos/import",
        rest([&](taar::multipart_form form)
        {
            bool const done = form.value("done").value_or("false") == "true";
            int imported = 0;
            if (auto const* list = form.find("list"))
            {
                std::string line;
                for (char c : list->data)
                {
                    if (c == '\n') { if (!line.empty()) { store.create({line, done}); ++imported; } line.clear(); }
                    else if (c != '\r') line += c;
                }
                if (!line.empty()) { store.create({line, done}); ++imported; }
            }
            return json::value {{"imported", imported}};
        },
        multipart_form_arg {description {"Fields: list (file, one title per line), done (true/false)"}}),
        openapi.describe({.summary = "Import todos from a text file", .tags = {"todos"}}));

    session.register_request_handler(
        method == http::verb::get && target == "/openapi.json",
        openapi.handler());

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

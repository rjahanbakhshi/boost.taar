# boost.taar

[![CI](https://github.com/rjahanbakhshi/boost.taar/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/rjahanbakhshi/boost.taar/actions/workflows/ci.yml)
[![B2](https://github.com/rjahanbakhshi/boost.taar/actions/workflows/b2.yml/badge.svg?branch=master)](https://github.com/rjahanbakhshi/boost.taar/actions/workflows/b2.yml)
[![Docs](https://github.com/rjahanbakhshi/boost.taar/actions/workflows/docs.yml/badge.svg?branch=master)](https://github.com/rjahanbakhshi/boost.taar/actions/workflows/docs.yml)
[![License: BSL-1.0](https://img.shields.io/badge/License-BSL--1.0-blue.svg)](https://www.boost.org/LICENSE_1_0.txt)

A C++20 header-only library for building HTTP services on Boost.Asio,
Boost.Beast, Boost.URL and Boost.JSON. Write a plain C++ function once and
serve it as a **REST** route, a **JSON-RPC 2.0** method and an **MCP**
(Model Context Protocol) tool, with an **OpenAPI** document generated from
the same registrations.

Documentation: <https://rjahanbakhshi.github.io/boost.taar/>

<!--toc:start-->
- [boost.taar](#boosttaar)
  - [Features](#features)
  - [Requirements](#requirements)
  - [Quick look](#quick-look)
    - [A server](#a-server)
    - [One function, three surfaces](#one-function-three-surfaces)
    - [JSON-RPC 2.0](#json-rpc-20)
    - [MCP](#mcp)
    - [Static files](#static-files)
    - [Streaming responses](#streaming-responses)
    - [Authorization with an interceptor](#authorization-with-an-interceptor)
  - [Examples](#examples)
  - [Configuring the build directory](#configuring-the-build-directory)
    - [No package manager](#no-package-manager)
    - [Using Conan 2 package manager](#using-conan-2-package-manager)
    - [Windows (MSVC) with Conan 2](#windows-msvc-with-conan-2)
  - [To build the code after configuring](#to-build-the-code-after-configuring)
  - [To run the unit-tests after the build](#to-run-the-unit-tests-after-the-build)
  - [To build and run the examples](#to-build-and-run-the-examples)
  - [Conan: creating and uploading](#conan-creating-and-uploading)
  - [Tested compilers and platforms](#tested-compilers-and-platforms)
  - [Roadmap](#roadmap)
  - [License](#license)
<!--toc:end-->

## Features

- **HTTP server**: coroutine-based sessions over TCP and TLS, keep-alive and
  pipelining, a composable matcher DSL (method, path templates with
  placeholders, headers, cookies), `405` with `Allow`, `HEAD` served from
  `GET`, multi-threaded `io_context` with a strand per connection, and
  graceful shutdown.
- **REST handlers**: arguments extracted from path, query, headers,
  cookies and bodies (JSON, forms, multipart, streamed) and converted to the
  callable's parameter types; return values converted to responses;
  declarative validation with `constrained`; chunked and server-sent-event
  streaming.
- **JSON-RPC 2.0**: named and positional parameters, batches,
  notifications, error mapping, request cancellation.
- **MCP**: Streamable HTTP and stdio transports, stateful sessions, tools
  with generated input and output schemas, resources and RFC 6570
  templates, prompts and completion, subscriptions, progress, logging,
  cancellation, server-to-client requests (sampling, elicitation, roots),
  binary content, stream resumption.
- **OpenAPI 3.1**: documents generated from the registered routes,
  including parameters, bodies, response schemas from `boost::describe`d
  types, defaults, constraints and security schemes.
- **Static files**: safe path resolution, MIME types, ETag and
  Last-Modified with conditional requests, byte ranges, precompressed
  variants, cache-control rules, SPA fallback, metadata and content caches.
- **State and policy**: per-connection contexts, cookie or header keyed
  sessions, interceptors and route guards for authorization, CORS.
- **One error model**: `taar::failure` with nested causes, `taar::result<T>`
  that throws on access, and per-surface mapping to HTTP status, JSON-RPC
  errors and MCP results.

## Requirements

- A C++20 compiler (see [tested compilers](#tested-compilers-and-platforms));
  newer standards are supported and tested in CI.
- Boost 1.84 or newer (Asio, Beast, URL, JSON, System, Describe; 1.86 via
  Conan). OpenSSL for TLS.
- CMake 3.28 or newer.

## Quick look

The snippets assume:

```C++
namespace taar = boost::taar;
namespace http = boost::beast::http;
using taar::matcher::method;
using taar::matcher::target;
```

Complete programs are in [example/](example).

### A server

```C++
boost::asio::io_context io;

taar::session::http session;
session.register_request_handler(
    method == http::verb::get && target == "/hello",
    taar::handler::rest([] { return "Hello, world!"; }));

taar::cancellation_signals signals;
boost::asio::signal_set os_signals {io, SIGINT, SIGTERM};
os_signals.async_wait([&](auto, auto) { signals.emit(); });   // graceful shutdown

boost::asio::co_spawn(io,
    taar::server::tcp("0.0.0.0", "8080", session, signals),
    taar::ignore_and_rethrow);
io.run();
```

### One function, three surfaces

```C++
int add(int a, int b) { return a + b; }

// REST: GET /api/add/2/3 -> 5
session.register_request_handler(
    method == http::verb::get && target == "/api/add/{a}/{b}",
    taar::handler::rest(&add, taar::handler::path_arg("a"), taar::handler::path_arg("b")),
    openapi.describe("Add two numbers"));

// JSON-RPC: {"jsonrpc":"2.0","id":1,"method":"add","params":{"a":2,"b":3}}
rpc.method("add", &add, taar::jsonrpc::rpc_arg("a"), taar::jsonrpc::rpc_arg("b"));

// MCP: a tool whose inputSchema is derived from the parameter types
mcp.tool("add", "Add two numbers", &add,
    taar::mcp::mcp_arg("a", "First addend"), taar::mcp::mcp_arg("b", "Second addend"));
```

### JSON-RPC 2.0

```C++
taar::jsonrpc::endpoint<> rpc;
rpc.method("greet",
    [](std::string name, std::optional<std::string> title)
    {
        return title ? *title + " " + name : name;
    },
    taar::jsonrpc::rpc_arg("name"), taar::jsonrpc::rpc_arg("title"));

session.register_request_handler(
    method == http::verb::post && target == "/rpc", rpc.handler());
```

Batches, notifications (`202`), positional parameters, `-32602` for bad
arguments and cancellation through `request_scope` are built in.

### MCP

```C++
struct notes { std::vector<std::string> items; };   // per-session state

taar::mcp::endpoint<notes> mcp {{.name = "notes", .version = "1.0.0"}};
mcp.tool("notes/add", "Append a note",
    [](notes& n, std::string text) { n.items.push_back(text); return n.items.size(); },
    taar::mcp::mcp_arg("text", "The note"));
mcp.resource("notes://{index}", "text/plain", "One note",
    [](notes const& n, std::size_t index) { return n.items.at(index); },
    taar::mcp::uri_arg("index"));

session.register_request_handler(method == http::verb::get && target == "/mcp", mcp.stream_handler());
session.register_request_handler(target == "/mcp", mcp.handler());

// or, for desktop clients that launch the server locally:
boost::asio::co_spawn(io, mcp.serve_stdio(), boost::asio::detached);
```

A tool may be a coroutine, report progress and be cancelled through
`request_scope`, ask the user with `co_await mcp.elicit(...)`, return images
or embedded resources, and notify its own client.

### Static files

```C++
taar::handler::htdocs_options options;
options.root = "./dist";
options.fallback_document = "index.html";                       // single-page apps
options.cache_rules = taar::handler::cache_rules::immutable_hashed();

session.register_request_handler(
    method == http::verb::get && target == "/{*path}",
    taar::handler::htdocs {std::move(options)});
```

### Streaming responses

```C++
session.register_request_handler(
    method == http::verb::get && target == "/ticks",
    taar::handler::rest([]() -> taar::chunked_response<std::string>
    {
        co_yield taar::set_header(http::field::content_type, "text/event-stream");
        boost::asio::steady_timer timer {co_await boost::asio::this_coro::executor};
        for (int i = 0; i < 10; ++i)
        {
            co_yield "data: " + std::to_string(i) + "\n\n";
            timer.expires_after(std::chrono::seconds {1});
            co_await timer.async_wait(taar::use_awaitable);
        }
    }));
```

### Authorization with an interceptor

```C++
session.add_interceptor(target != "/health",
    [](http::request_header<> const& request, taar::matcher::context& ctx) -> taar::interceptor_result
    {
        auto auth = request.find(http::field::authorization);
        if (auth == request.end() || !auth->value().starts_with("Bearer "))
            return taar::reject(http::status::unauthorized);
        ctx.attributes.set(principal {verify(auth->value().substr(7))});
        return taar::proceed();
    });

// any handler, method or tool can then ask for it:
taar::handler::rest([](principal const& who) { return who.name; },
    taar::handler::attribute_arg<principal>());
```

## Examples

| Example | Shows |
|---|---|
| [hello_server](example/hello_server.cpp) | the smallest server |
| [rest_crud_server](example/rest_crud_server.cpp) | a REST API with validation, multipart import and OpenAPI |
| [jsonrpc_server](example/jsonrpc_server.cpp) | JSON-RPC methods, batches, error mapping |
| [mcp_server](example/mcp_server.cpp) | an MCP server over Streamable HTTP |
| [mcp_stdio_server](example/mcp_stdio_server.cpp) | the same tools over stdio |
| [taskboard_server](example/taskboard_server.cpp) | one service on REST, JSON-RPC and MCP with interceptors |
| [htdocs_server](example/htdocs_server.cpp) | static files and a single-page app |
| [streaming_server](example/streaming_server.cpp) | chunked and SSE responses, streamed uploads |
| [cancellation_server](example/cancellation_server.cpp) | cancellation of streams, jobs and tools |
| [cookie_session_server](example/cookie_session_server.cpp) | cookie sessions |
| [connection_state_server](example/connection_state_server.cpp) | per-connection state |
| [tls_server](example/tls_server.cpp) | HTTPS |
| [websocket_server](example/websocket_server.cpp) | WebSocket upgrade through a raw handler |
| [proxy_server](example/proxy_server.cpp) | a forwarding proxy |

## Configuring the build directory

Execute the following commands from the project's root directory to configure
it for the Debug configuration.

### No package manager

This requires the Boost libraries to be installed on the host.

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
```

### Using Conan 2 package manager

```bash
cmake \
    -B build \
    -S . \
    -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=cmake/conan_provider.cmake \
    -DCMAKE_BUILD_TYPE=Debug
```

### Windows (MSVC) with Conan 2

Two project-local profiles are provided:

- `profiles/windows-vs2022.profile`: Visual Studio 2022, MSVC v143
  (`compiler.version=194`).
- `profiles/windows-vs2026.profile`: Visual Studio 2026, MSVC v145
  (`compiler.version=195`).

Both target `compiler.cppstd=20` (pass `-s compiler.cppstd=23` for a newer
standard) and force the Ninja generator for Conan
dependency builds. From a Visual Studio Developer Command Prompt for the
matching VS version:

```cmd
conan install . -of=build ^
    -pr:h=profiles/windows-vs2022.profile ^
    -pr:b=profiles/windows-vs2022.profile ^
    --build=missing
cmake --preset conan-release
cmake --build --preset conan-release
ctest --preset conan-release
```

Notes:
- `conancenter` does not currently have prebuilt Boost binaries for
  every `compiler.cppstd` on these MSVC versions, so the first
  `conan install` will build Boost from source. Subsequent runs reuse
  the cached package.
- The CMake on the system `PATH` must be at least 3.28 and recent enough
  for the matching Visual Studio generator. The CMake bundled with
  Visual Studio (under `Common7\IDE\CommonExtensions\Microsoft\CMake\`)
  satisfies this: VS 2022 17.14 ships CMake 3.31, VS 2026 ships
  CMake 4.2. If you launch from PowerShell, prepend that bundled
  `CMake\bin` (and `Ninja`) directory to `PATH` and source `vcvars64.bat`
  before running the commands above.

## To build the code after configuring

Assuming it is invoked from the project's root directory.

```bash
cmake --build build
```

## To run the unit-tests after the build

```bash
build/test/boost-taar-test
```

## To build and run the examples

```bash
cmake --build build -j --target hello_server && build/example/hello_server 8082
# all of them: cmake --build build -j --target examples
```

## Conan: creating and uploading

```bash
version=$(git describe --abbrev=0 | grep -o '[0-9]\+.[0-9]\+.[0-9]\+')
conan create . --version "$version" --build=missing
conan upload "boost-taar/$version" --only-recipe --remote your_conan_remote
```

## Tested compilers and platforms

C++20 is the minimum. CI builds and runs the tests with:

| Standard | Toolchains |
|---|---|
| C++20 | GCC 13, Clang 18 (libstdc++ and libc++), MSVC 2022 (v143), Apple Clang 17 (Xcode 16.4) |
| C++23 | GCC 14, Clang 20, MSVC 2026 (v145), Apple Clang 21 (Xcode 26.6) |
| C++26 | GCC 15, Clang 22 with libc++ (experimental) |

plus AddressSanitizer/UndefinedBehaviorSanitizer (GCC 14) and
ThreadSanitizer (Clang 18) jobs, experimental probes of GCC 12 and Clang 17
below the supported floor, and the B2 build in the Boost superproject with
GCC 13 (C++20) and Clang 18 (C++23). The macOS jobs are experimental while
two macOS-only test failures are open.

Notes:
- CMake 3.28 or newer is required; Ubuntu 22.04 ships 3.22.
- On Windows, use the CMake bundled with Visual Studio (3.31 in VS 2022, 4.2
  in VS 2026); system CMake 3.27 is too old.

## Roadmap

Known gaps and deliberately out-of-scope features are tracked in
[ROADMAP.md](ROADMAP.md); changes per release are in the
[release notes](https://rjahanbakhshi.github.io/boost.taar/boost_taar/release_notes.html).

## License

Copyright (c) 2021-2026 Reza Jahanbakhshi

Distributed under the Boost Software License, Version 1.0. (See accompanying
file LICENSE or copy at [http://www.boost.org/LICENSE_1_0.txt](http://www.boost.org/LICENSE_1_0.txt))

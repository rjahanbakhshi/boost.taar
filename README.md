# boost.web

***⚠️ Warning: This library is not production ready and is a work in progress. Use
with care.***

A header-only library created to facilitate web server development built on top
of other boost libraries like boost.beast, boost.asio, boost.url, boost.json, etc.

<!--toc:start-->
- [boost.web](#boostweb)
  - [Overview](#overview)
    - [Requirements](#requirements)
    - [Quick Look](#quick-look)
      - [REST API handler with stock response](#rest-api-handler-with-stock-response)
      - [REST API handler with JSON response](#rest-api-handler-with-json-response)
      - [REST API handler for POST method and automatic stock response](#rest-api-handler-for-post-method-and-automatic-stock-response)
      - [Document handler for GET method serving documents from the specified root path](#document-handler-for-get-method-serving-documents-from-the-specified-root-path)
      - [Custom handler for PUT method](#custom-handler-for-put-method)
  - [Configuring the build directory](#configuring-the-build-directory)
    - [No package manager](#no-package-manager)
    - [Using Conan 2 package manager](#using-conan-2-package-manager)
  - [To build the code after configuring](#to-build-the-code-after-configuring)
  - [To run the unit-tests after the build](#to-run-the-unit-tests-after-the-build)
  - [To install under the local prefix directory ./out](#to-install-under-the-local-prefix-directory-out)
  - [To build and run the examples](#to-build-and-run-the-examples)
  - [License](#license)
<!--toc:end-->

## Overview

Boost.web is a portable C++ header-only library which provides convenient tools
for web server development. This library is built on top of other boost libraries,
like Boost.ASIO, Boost.Beast, Boost.URL, and Boost.JSON, and Boost.system.

### Requirements

- A C++ compiler supporting C++23 and above.
- Other boost libraries like  Boost.ASIO, Boost.Beast, Boost.URL, and Boost.JSON,
and Boost.system.

### Quick Look

Below are some short examples showcasing various use-cases for this library. For
complete examples, look under the examples directory.

#### REST API handler with stock response

Accepts an HTTP GET request for specific target and respond with a stock text.

```C++
web::session::http http_session;
http_session.register_request_handler(
    method == http::verb::get && target == "/api/version",
    web::handler::rest([]{ return "1.0"; }
));

web::cancellation_signals cancellation_signals;
co_spawn(
    io_context,
    web::server::tcp(
        "0.0.0.0",
        "8090",
        http_session,
        cancellation_signals),
    bind_cancellation_slot(cancellation_signals.slot(), web::ignore_and_rethrow));

io_context.run();
```

#### REST API handler with JSON response

Accepts an HTTP GET request with a path template, calculates the sum for two of
the path arguments and respond with a JSON value.

```C++
web::session::http http_session;
http_session.register_request_handler(
    method == http::verb::get && target == "/api/sum/{a}/{b}",
    web::handler::rest([](int a, int b)
    {
        return boost::json::value {
            {"a", a},
            {"b", b},
            {"result", a + b}
        };
    },
    web::handler::path_arg("a"),
    web::handler::path_arg("b")
));

web::cancellation_signals cancellation_signals;
co_spawn(
    io_context,
    web::server::tcp(
        "0.0.0.0",
        "8090",
        http_session,
        cancellation_signals),
    bind_cancellation_slot(cancellation_signals.slot(), web::ignore_and_rethrow));

io_context.run();
```

#### REST API handler for POST method and automatic stock response

Accepts an HTTP POST request for a specific target, expects that a query parameter
called "test_arg" to be present and passes the value of it to the handler lambda.

```C++
web::session::http http_session;
http_session.register_request_handler(
    method == http::verb::post && target == "/api/store/",
    web::handler::rest([](const std::string& test_arg)
    {
        std::cout << "Received test_arg = " << test_arg << '\n';
    },
    web::handler::query_arg("test_arg")
));

web::cancellation_signals cancellation_signals;
co_spawn(
    io_context,
    web::server::tcp(
        "0.0.0.0",
        "8090",
        http_session,
        cancellation_signals),
    bind_cancellation_slot(cancellation_signals.slot(), web::ignore_and_rethrow));

io_context.run();
```

#### Document handler for GET method serving documents from the specified root path

Accepts an HTTP GET request for all targets under the specified template and respond
with the requested file content with a correct mime-type.

```C++
web::session::http http_session;
http_session.register_request_handler(
    method == http::verb::get && target == "/{*}",
    web::handler::htdocs {argv[2]}
);

web::cancellation_signals cancellation_signals;
co_spawn(
    io_context,
    web::server::tcp(
        "0.0.0.0",
        "8090",
        http_session,
        cancellation_signals),
    bind_cancellation_slot(cancellation_signals.slot(), web::ignore_and_rethrow));

io_context.run();
```

#### Custom handler for PUT method

Accepts an HTTP PUT request for all targets under the specified template and
respond with a custom text message.

```C++
web::session::http http_session;
http_session.register_request_handler(
    method == http::verb::put && target == "/special/{*}",
    [](
        const http::request<http::empty_body>& request,
        const web::matcher::context& context) -> http::message_generator
    {
        http::response<http::string_body> res {
            boost::beast::http::status::ok,
            request.version()};
        res.set(boost::beast::http::field::content_type, "text/html");
        res.keep_alive(request.keep_alive());
        res.body() = "Special path is: " + context.path_args.at("*");
        res.prepare_payload();
        return res;
    }
);

web::cancellation_signals cancellation_signals;
co_spawn(
    io_context,
    web::server::tcp(
        "0.0.0.0",
        "8090",
        http_session,
        cancellation_signals),
    bind_cancellation_slot(cancellation_signals.slot(), web::ignore_and_rethrow));

io_context.run();
```

## Configuring the build directory

Below are the commands to be executed from the project's root directory to build
the Debug configuration.

### No package manager

This requires that the boost libraries are installed on the host.

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

## To build the code after configuring

Assuming that it's invoked from the root directory of the project.

```bash
cmake --build build
```

## To run the unit-tests after the build

```bash
build/test/boost-web-test
```

## To install under the local prefix directory ./out

```bash
cmake --install build --prefix out
```

## To build and run the examples

```bash
cmake --build build -j --target http_server && build/examples/http_server 8082 ./
```

## Creating and uploading the Conaan

```bash
version=$(git describe --abbrev=0 | grep -o '[0-9]\+.[0-9]\+.[0-9]\+')
conan create . --version "$version" --build=missing
conan upload "boost-web/$version" --only-recipe --remote your_conan_remote
```

## License

Copyright © 2021, 2024 Reza Jahanbakhshi

Distributed under the Boost Software License, Version 1.0. (See accompanying
file LICENSE or copy at [http://www.boost.org/LICENSE_1_0.txt](http://www.boost.org/LICENSE_1_0.txt))

## TODO

- http session needs full unit-test coverage.
- htdocs handler needs full unit-test-coverage.
- curl -v 127.0.0.1:8082/api/sum/1/2 doesn't match
- rest should only handle its own internal errors and leave the rest to the
http handler.
- rest_arg content-type interpretation could be wrong.
- encoded and decoded option for rest arg providers.

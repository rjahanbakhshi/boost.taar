# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

boost.taar is a C++23 header-only library for HTTP web server/client development, built on Boost.ASIO, Boost.Beast, Boost.URL, and Boost.JSON. Licensed under Boost Software License 1.0.

## Build Commands

```bash
# Configure (with Conan 2 for dependency management)
cmake -B build -S . -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=cmake/conan_provider.cmake -DCMAKE_BUILD_TYPE=Debug

# Configure (without Conan, if Boost is already installed)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Build everything
cmake --build build

# Run all tests
build/test/boost-taar-test

# Run a specific test case (Boost.Test filter)
build/test/boost-taar-test --run_test=<test_case_name>

# Run tests via CTest
ctest --test-dir build

# Build and run a specific example
cmake --build build --target http_server && build/examples/http_server 8082 ./

# Install
cmake --install build --prefix out
```

Optional CMake flags: `ENABLE_CLANG_TIDY`, `ENABLE_UNITY_BUILD`.

## Architecture

All library code lives under `boost/taar/` as headers (`.hpp`). Namespace: `boost::taar::<module>`.

### Key Modules

- **`session/http.hpp`** — HTTP session coroutine managing request/response lifecycle, keep-alive, and error handling. Registers matchers paired with request handlers. Central orchestration point.

- **`server/tcp.hpp`** — TCP acceptor coroutine. Binds, listens, and spawns HTTP sessions per connection. Integrates with ASIO cancellation signals.

- **`matcher/`** — Composable request matching DSL. Matchers inspect method, path, headers, cookies, version. Supports operator composition (`&&`, `||`, `!`, `==`, `!=`). `target` supports path templates like `/api/{id}` and greedy params `/{*path}`. `context.hpp` carries extracted path arguments as `unordered_map`.

- **`handler/`** — Request handler wrappers. `rest.hpp` wraps callables with automatic argument extraction and response generation. `rest_arg.hpp` defines argument providers (path params, query params, headers, body, etc.). `rest_arg_cast.hpp` handles type conversion. `htdocs.hpp` serves static files.

- **`core/`** — Shared utilities. `response_from.hpp` uses tag_invoke for extensible type-to-HTTP-response conversion. `response_builder.hpp` provides a builder pattern. `awaitable.hpp` aliases ASIO awaitable types. `cancellation_signals.hpp` manages thread-safe shutdown. `error.hpp` defines the custom error category.

- **`type_traits/`** — Compile-time introspection: callable signature inspection, template specialization detection, string-like type concepts.

### Key Design Patterns

- **Tag invoke** (`response_from_tag.hpp`, `rest_arg_cast_tag.hpp`): Extension points for custom types — implement `tag_invoke` overloads to teach the library how to convert your types.
- **Coroutines throughout**: All async operations use `co_await`/`co_return` with `awaitable<T>` (aliased to `asio::awaitable<T, io_context::executor_type>`).
- **Matcher DSL**: Expression-template style composition via `operand.hpp` operator overloads.
- **Soft vs hard errors**: Soft errors are application-level (recoverable, overridable via `set_soft_error_handler`). Hard errors are connection-level (overridable via `set_hard_error_handler`).

## Testing

Uses Boost.Test. Test entry point: `test/test.cpp`. All test files are compiled into a single `boost-taar-test` binary. Tests use `BOOST_AUTO_TEST_CASE` and `BOOST_TEST` assertions. Many tests include `static_assert` for compile-time verification of type traits and concepts.

## Coding Conventions

- **East const:** Use east-const style (`T const&`, `int const x`) not west-const (`const T&`, `const int x`).

- **CMakeLists.txt:** When adding new files, always update the relevant `CMakeLists.txt`:
  - New headers in `boost/taar/` must be added to the root `CMakeLists.txt` under `target_sources(... FILE_SET HEADERS ...)`
  - New test files must be added to `test/CMakeLists.txt`
  - New examples must be added to `examples/CMakeLists.txt`
  - Maintain alphabetical order within each directory group (e.g., all `core/` headers together, sorted alphabetically)

## Requirements

- C++23 compiler
- CMake 3.28+
- Boost 1.84+ (1.86 via Conan)

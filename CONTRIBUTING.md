# Contributing to Boost.Taar

Thank you for your interest in Boost.Taar. This document describes how to
report issues, propose changes, and prepare patches that fit the rest of the
library.

## Reporting issues

- Search the existing issues at
  <https://github.com/rjahanbakhshi/boost.taar/issues> before opening a new
  one.
- Include the compiler and version, the operating system, the Boost version,
  the CMake command used to configure, and the command line used to build.
- For bugs, please attach a minimal reproducer in the form of a single
  translation unit, ideally one that fits inside `examples/`. A reproducer
  that compiles against the current `master` is much easier to act on than a
  textual description.
- For build problems, please paste the full `cmake` and compiler output, not
  a summarised excerpt.

## Asking questions

Open a discussion at
<https://github.com/rjahanbakhshi/boost.taar/discussions>. Please reserve
issues for bug reports, feature requests, and documentation gaps.

## Proposing changes

Before opening a pull request that adds new public surface (a new matcher, a
new argument provider, a new handler, a new core utility), open an issue or
discussion to agree on the shape of the API first. This avoids the case
where a thoughtful patch lands but cannot be merged because it conflicts
with an unstated design constraint.

Bug fixes, documentation improvements, build-system fixes, and additional
test coverage do not need a prior discussion — feel free to open a PR
directly.

## Pull request workflow

1. Fork the repository and create a feature branch off `master`.
2. Make your changes. Keep commits focused — one concern per commit, with a
   commit message that explains the *why*, not just the *what*.
3. Add or update tests in `test/` so that the change is covered by the
   existing `boost-taar-test` binary. Compile-time properties (concepts,
   type traits, tag-invoke extension points) are usually best exercised with
   `static_assert` inside a `BOOST_AUTO_TEST_CASE`.
4. Run the full test suite locally:

   ```bash
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   build/test/boost-taar-test
   ```
5. Make sure the examples still build:

   ```bash
   cmake --build build --target http_server awaitable_server \
       chunked_server htdocs_server mixed_server response_stream \
       websocket_server
   ```
6. Push to your fork and open a pull request against `master`. In the PR
   description, describe the motivation, the user-visible change, and any
   trade-offs.

By submitting a pull request you assert that you have the right to license
your contribution under the Boost Software License, Version 1.0, and that
you agree to do so.

## Testing with b2

The CMake build above is enough for day-to-day work. Patches that touch
`build.jam`, `test/Jamfile`, `meta/libraries.json`, or `doc/` additionally
need to be verified against the Boost B2 build, because that is the build
that runs when taar is consumed as part of a Boost super-project.

Set up a Boost super-project checkout once:

```bash
git clone --recursive https://github.com/boostorg/boost.git \
    ~/src/boost-modular
cd ~/src/boost-modular && ./bootstrap.sh
```

Then symlink (or `git submodule add`) your working copy of taar into
`libs/`:

```bash
ln -s ~/proj/boost.taar ~/src/boost-modular/libs/taar
```

Run the tests with B2:

```bash
cd ~/src/boost-modular
./b2 libs/taar/test cxxstd=23
```

Build the documentation. This requires `doxygen`, `xsltproc`, and the
`docbook-xsl` stylesheets to be installed on your system; B2 compiles
QuickBook itself the first time:

```bash
./b2 libs/taar/doc
```

Generated HTML lands in `libs/taar/doc/html/`. Open `index.html` in a
browser to sanity-check that the narrative chapters in `doc/qbk/` render
and that the reference section pulled from the Doxygen comments looks
right.

## Coding conventions

The conventions here exist so that any header reads the same way as any
other. New code should follow them; reformatting existing code to match
should be a separate commit from any behavioural change.

### Language and headers

- Boost.Taar is a header-only C++23 library. New code must compile cleanly
  under the supported compilers (see the README), with `-Wall -Wextra` or
  the MSVC equivalent.
- Every header lives under `include/boost/taar/<module>/<name>.hpp`. The
  include guard mirrors the path, e.g.
  `BOOST_TAAR_MATCHER_TARGET_HPP`.
- Every header begins with the standard Boost copyright/licence preamble
  followed by the include guard, the `#include` block (with library
  includes grouped together), and the namespace open.
- Headers are self-contained: a translation unit that includes only one of
  the public headers must compile.
- Implementation details belong in a nested `detail` namespace and, when
  they are large enough to warrant their own file, in a
  `<module>/detail/` subdirectory.

### Style

- Use east-const: `T const&`, `int const x`, not `const T&` / `const int x`.
- Indent with four spaces, no tabs.
- Brace on its own line for namespaces, classes, and functions; opening
  brace on the same line for control flow.
- Prefer `auto` for iterator and template-result types, named types for
  parameter declarations.
- Names are `lower_snake_case` for functions, variables, namespaces, and
  most types. Concepts and type traits follow the standard-library style
  (`is_foo_v`, `foo_t`, etc.).
- Avoid raw `new` / `delete`. Use smart pointers, value types, or coroutine
  awaitables as appropriate.

### Public API surface

- Async operations return `boost::taar::awaitable<T>` and use
  `co_await`/`co_return`.
- Extension points are exposed as `tag_invoke` overloads, not virtual
  functions or CRTP. The library provides the tag in a `*_tag.hpp` header;
  user code customises by overloading the tag.
- Errors that the application can recover from are reported as soft errors
  through the session's soft-error handler. Errors that take down the
  connection are reported as hard errors. Throwing from user-supplied
  handlers is supported but discouraged.

### Tests

- Tests use Boost.Test. Each `test/test_*.cpp` file is compiled into the
  single `boost-taar-test` binary defined by `test/CMakeLists.txt`. New
  test files must be added to that list.
- Test cases are declared with `BOOST_AUTO_TEST_CASE` and assertions with
  `BOOST_TEST`.
- Prefer `static_assert` for compile-time invariants — concept satisfaction,
  trait results, signature deductions — even when a runtime test also
  exists.

### CMake

- When you add a new public header, add it to the alphabetically-sorted
  `target_sources(... FILE_SET HEADERS ...)` block in the root
  `CMakeLists.txt`, *and* to the build-system manifests
  (`build.jam`, the Doxygen input list in `doc/`, and `meta/libraries.json`
  if appropriate).
- New test files go into `test/CMakeLists.txt` in alphabetical order.
- New examples go into `examples/CMakeLists.txt`.

## Licence

By contributing you agree that your contribution is released under the
Boost Software License, Version 1.0. See
<https://www.boost.org/LICENSE_1_0.txt> or the `LICENSE` file in the
repository.

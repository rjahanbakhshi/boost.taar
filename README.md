# boost.web

A header-only library created to facilitate web server development built on top
of other boost libraries like boost.beast, boost.asio, boost.url, boost.json, etc.

<!--toc:start-->
- [boost.web](#boostweb)
  - [Configuring the build directory](#configuring-the-build-directory)
    - [No package manager](#no-package-manager)
    - [Using Conan 2 package manager](#using-conan-2-package-manager)
  - [To build the code after configuring](#to-build-the-code-after-configuring)
  - [To run the unit-tests after the build](#to-run-the-unit-tests-after-the-build)
  - [To install under the local prefix directory ./out](#to-install-under-the-local-prefix-directory-out)
<!--toc:end-->

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

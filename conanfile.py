import os
from conan import ConanFile
from conan.tools.files import copy
from conan.tools.cmake import cmake_layout, CMake

class BoostTaar(ConanFile):
    name = "boost-taar"
    version = "0.0.3"
    license = "Boost Software License, Version 1.0"
    author = "Reza Jahanbakhshi <reza.jahanbakhshi@gmail.com>"
    url = "https://github.com/rjahanbakhshi/boost.taar"
    description = "A header-only library for web server and client development"
    topics = ("boost", "asio", "http", "network", "web", "taar")
    settings = "os", "arch", "compiler", "build_type"
    exports_sources = [
        "boost/*",
        "cmake/*",
        "examples/*",
        "test/*",
        "CMakeLists.txt",
        "LICENSE",
        "README.md"]
    no_copy_source = True
    generators = "CMakeToolchain", "CMakeDeps"

    def requirements(self):
        self.requires("boost/1.84.0")

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        if not self.conf.get("tools.build:skip_test", default=False):
            self.run(os.path.join(self.cpp.build.bindir, "test/boost-taar-test"))
        cmake.install()

    def package(self):
        copy(self, "LICENSE", self.source_folder, self.package_folder)

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.includedirs = ["include"]

    def package_id(self):
        self.info.clear()

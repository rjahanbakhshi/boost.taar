from conan import ConanFile
from conan.tools.cmake import cmake_layout

class SessionHub(ConanFile):
    name = "boost-web"
    version = "1.0"
    generators = "CMakeDeps"
    settings = "os", "compiler", "arch", "build_type"

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        self.requires("boost/1.84.0")


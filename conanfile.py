import os
from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import copy

class RingCacheConan(ConanFile):
    name = "ringcache"
    version = "0.1.0"
    license = "MIT"
    author = "RingCache Contributors"
    url = "https://github.com/John-elden-ring/Ring-Cache"
    description = "Distributed in-memory key-value cache using consistent hashing"
    topics = ("cache", "distributed", "consistent-hashing", "key-value", "cpp17")
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [False, True],
        "fPIC": [False, True],
    }
    default_options = {
        "shared": False,
        "fPIC": True,
    }

    exports_sources = (
        "CMakeLists.txt",
        "LICENSE",
        "include/*",
        "src/*",
        "cmake/*",
        "apps/*",
        "tests/*"
    )

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["BUILD_TESTING"] = False
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build(target="ringcache_client")

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.libs = ["ringcache_client"]
        self.cpp_info.set_property("cmake_file_name", "RingCache")
        self.cpp_info.set_property("cmake_target_name", "RingCache::client")
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["ws2_32", "mswsock"]

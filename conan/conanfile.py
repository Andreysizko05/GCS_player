
import os
from os.path import join

from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain
from conan.tools.env import VirtualRunEnv
from conan.tools.files import copy

class PID_GUI(ConanFile):
    name = "pid_gui"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    build_policy = "missing"

    # conan remote add kramm_win_mvsc_reldeb_c2.6 http://192.168.9.119:9304 --index 1 --insecure

    def requirements(self):
        self.requires("qt/6.8.3")
        self.requires("libiconv/1.18", override=True)

    def configure(self):
        pass

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["CMAKE_FROM_CONAN"] = True
        deps = CMakeDeps(self)
        tc.generate()
        deps.generate()
        runenv = VirtualRunEnv(self)
        runenv.generate()

        for dep in self.dependencies.values():
            if len(dep.cpp_info.libdirs) > 0:
                print(f"[CONAN DEBUG] Absolute lib Folder: {dep.cpp_info.bindirs[0]}")
                copy(self, "*.dll", dep.cpp_info.libdirs[0], f'{self.build_folder}/../{self.settings.build_type}')
                copy(self, "*.so*", dep.cpp_info.libdirs[0], f'{self.build_folder}/..')
                plugins_dir = join(dep.cpp_info.libdirs[0], "archdatadir", "plugins")
                copy(self, "*.dll", plugins_dir, f'{self.build_folder}/../{self.settings.build_type}')
    
            if len(dep.cpp_info.bindirs) > 0:
                print(f"[CONAN DEBUG] Absolute bin Folder: {dep.cpp_info.bindirs[0]}")
                copy(self, "*.dll", dep.cpp_info.bindirs[0], f'{self.build_folder}/../{self.settings.build_type}')
                copy(self, "*.so*", dep.cpp_info.bindirs[0], f'{self.build_folder}/..')
                plugins_dir = join(dep.cpp_info.bindirs[0], "archdatadir", "plugins")
                copy(self, "*.dll", plugins_dir, f'{self.build_folder}/../{self.settings.build_type}')

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        target = "PID_GUI"
        cmake.build(target=target)

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["PID_GUI"]
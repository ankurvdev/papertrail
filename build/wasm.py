import os
import pathlib
import shutil
import sys
import cmake
import externaltools


class WasmHandler(cmake.CMakeHandler):
    def __init__(self, srcdir: pathlib.Path, builddir: pathlib.Path | None = None, **kargs):
        super(WasmHandler, self).__init__("wasm", srcdir, builddir, **kargs)
        self.emsdk = externaltools.GetEmscripten()
        alreadypaths = set([pathlib.Path(onepath).absolute() for onepath in os.environ['PATH'].split(os.pathsep)])
        appendpaths = list([onepath.as_posix() for onepath in self.emsdk.paths if onepath.absolute() not in alreadypaths])
        appendpathsstr = os.pathsep.join(appendpaths)
        if len(appendpathsstr) > 0:
            newpath = os.pathsep.join([appendpathsstr, os.environ['PATH']])
            os.environ['PATH'] = newpath
            sys.stderr.write(f"PATH += {appendpathsstr}\n")
            sys.stderr.write(f"PATH = {newpath}\n")
        for envvarname, envvarval in self.emsdk.envvars.items():
            sys.stderr.write(f'{envvarname} = {envvarval}\n')
            os.environ[envvarname] = envvarval
        os.environ['CMAKE_CXX_COMPILER'] = 'em++'
        os.environ['CMAKE_C_COMPILER'] = 'emcc'
        self.toolchain = self.emsdk.cmake_toolchain_file

    def GetGenerator(self):
        if sys.platform == "win32" and shutil.which('make') is not None:
            return "Unix Makefiles"
        return self.generator_

    def GetGeneratorArgs(self, arch: str, _config: str) -> list[str]:
        return ["-DCMAKE_TOOLCHAIN_FILE=" + self.toolchain.as_posix()]

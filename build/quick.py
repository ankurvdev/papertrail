import configparser
import enum
import fnmatch
import graphlib
import os
import pathlib
import sys
from typing import Any, Dict, List, Set

import buildenv
import externaltools


class Platform(enum.Enum):
    windows = enum.auto()
    uwp = enum.auto()
    android = enum.auto()
    linux = enum.auto()
    ios = enum.auto()
    osx = enum.auto()
    emscripten = enum.auto()


class Type(enum.Enum):
    exe = enum.auto()
    lib = enum.auto()
    dll = enum.auto()
    test = enum.auto()
    npmresource = enum.auto()
    webapp = enum.auto()
    glapp = enum.auto()
    qtapp = enum.auto()


class Quick:
    @staticmethod
    def GetBuildPlatform():
        if sys.platform == "win32":
            return Platform.windows
        elif sys.platform == "linux":
            return Platform.linux
        else:
            raise Exception(f"Unknown Build Platform : {sys.platform}")

    def __init__(self, reporoot: pathlib.Path):
        self._reporoot: pathlib.Path = reporoot
        self._config = configparser.ConfigParser()
        self._generated = {}
        self._srcdir: pathlib.Path = pathlib.Path()
        self._buildplatform = Quick.GetBuildPlatform()

    def LoadConfig(self, inifiles: list[pathlib.Path]):
        self._config.read(list([inifile.as_posix() for inifile in inifiles]))
        self._srcdir: pathlib.Path = inifiles[0].parent.absolute()
        return self

    def VerifyLoaderCMake(self):
        projectname = buildenv.BuildEnv.GetProjectName(self._reporoot)
        cmakecontents = [
            "cmake_minimum_required(VERSION 3.19)",
            f"project({projectname})",
            "find_package(Quick)",
            "if (NOT Quick_FOUND)",
            "set(Quick_DIR \"${CMAKE_CURRENT_LIST_DIR}/../build/cmake\")",
            "find_package(Quick REQUIRED)",
            "endif()",
            "quickload_config(${CMAKE_CURRENT_LIST_DIR}/quickload.config.ini)",
        ]
        cmakecontents = "\n".join(cmakecontents)
        loadercmakepath = self._srcdir / "CMakeLists.txt"
        if loadercmakepath.exists() and loadercmakepath.read_text() == cmakecontents:
            return
        loadercmakepath.write_text(cmakecontents)

    def _GetSectionNames(self, name: str):
        target = self._config[name].get("Name", name)
        return (target, target.replace(".", "_"))

    def _GetResources(self, srcdir: pathlib.Path, resources: str) -> Dict[str, List[pathlib.Path]]:
        resgroups: Dict[str, List[str]] = {}
        retval: Dict[str, List[pathlib.Path]] = {}
        for f in resources.split(","):
            fname = f.split(":")
            resgrp = fname[0] if len(fname) > 0 else "resources"
            fname = fname[-1]
            resgroups.setdefault(resgrp, [])
            resgroups[resgrp].append(fname)

        for grp, fnames in resgroups.items():
            respaths: List[pathlib.Path] = []
            for f in fnames:
                fpath = (srcdir / f)
                if fpath.is_dir():
                    for ff in os.scandir(fpath):
                        respaths += [pathlib.Path(ff.path)]
                else:
                    respaths += [fpath]
            retval[grp] = respaths
        return retval

    def _GenerateCMakeForSection(self, name: str, outdir: pathlib.Path):
        info = self._config[name]

        links: List[str] = info.get("Link", "").split(",")
        publiclinks: List[str] = []
        privatelinks: List[str] = []
        for link in links:
            a, b = (link + ":").split(":", maxsplit=1)
            if b == '' or b == 'private:':
                privatelinks.append(a)
            elif b == "public:":
                publiclinks.append(a)
            else:
                raise Exception(f"Unexpected link modified {b}")
        target, fname = self._GetSectionNames(name)
        platforms = info.get("Platforms", ",".join(Platform.__members__.keys())).split(",")
        manifest = info.get("AppManifest", None)
        resources = info.get("Resources", None)
        packages = info.get("Packages", None)
        srcexclude = info.get("SrcExclude", "")
        secdir = info.get("Dir", ".")
        addlfiles = info.get("Files", "").split(",")
        sectype = info.get("Type")
        if sectype not in Type.__members__:
            raise Exception(f"Unknown type: {sectype} for name: {name}")
        sectype = Type[sectype]

        srcdir = (self._srcdir / secdir)
        packages = packages.split(",") if packages else []
        extensions = ["cpp", "c", "h", "pidl", "idl", "ly", "cmake"]
        files: List[Any] = []
        filegroups: Dict[str, List[pathlib.Path]] = {}
        filenames: List[str] = []
        pchcpp = None
        pchh = None
        for ext in extensions:
            filegroups.setdefault(ext, [])
        for f in os.scandir(self._srcdir / secdir):
            def exclude(fn: str, srcexclude: str):
                for excl in srcexclude.split(","):
                    if fnmatch.fnmatch(fn, excl):
                        return True
                return False

            if exclude(f.name, srcexclude):
                continue
            files.append({"name": f.name, "path": pathlib.Path(f.path)})
        for f in addlfiles:
            files.append({"name": pathlib.Path(f).name, "path": (self._srcdir / f)})
        for f in files:
            _fname = f["name"]
            path: pathlib.Path = f["path"]
            for ext in extensions:
                if fnmatch.fnmatch(_fname, "*." + ext):
                    filegroups[ext].append(path.absolute())
                    filenames.append(_fname)
            if _fname == "pch.h":
                pchh = path
            if _fname == "pch.cpp":
                pchcpp = path
            if not manifest and _fname == "app.manifest.ini":
                manifest = path

        cmakecontents: List[str] = []
        cmake_primary_target_type = {
            Type.exe: "executable",
            Type.lib: "library",
            Type.dll: "library",
            Type.test: "test_executable",
            Type.npmresource: "npm_build_embedded_resource",
            Type.webapp: "library",
            Type.glapp: "library",
            Type.qtapp: "qt_executable"
        }[sectype]
        cmake_secondary_target_type = {
            Type.exe: "",
            Type.lib: "STATIC",
            Type.dll: "SHARED",
            Type.test: "",
            Type.npmresource: "STATIC",
            Type.webapp: "SHARED",
            Type.glapp: "SHARED",
            Type.qtapp: "",
        }[sectype]
        target_srcs = '"\n"'.join([p.as_posix() for p in (filegroups["h"] + filegroups["cpp"] + filegroups["c"] + filegroups["idl"])])
        if sectype == Type.npmresource:
            target_srcs = srcdir
        create_target_cmake = f"""
add_{cmake_primary_target_type}({target} {cmake_secondary_target_type}
    "{target_srcs}")
target_include_directories({target} PUBLIC "{srcdir}")
target_include_directories({target} PUBLIC ${{CMAKE_CURRENT_BINARY_DIR}})
if(MSVC AND EXISTS "{pchcpp}" AND EXISTS "{pchh}")
    set_target_properties({target} PROPERTIES COMPILE_FLAGS /Yupch.h)
    set_source_files_properties({pchcpp} PROPERTIES COMPILE_FLAGS /Ycpch.h)
endif()
use_vcpkg({target} {" ".join(packages)})
if ("{cmake_secondary_target_type}" STREQUAL "SHARED")
    generate_export_header({target})
endif()
"""
        if len(privatelinks) > 0:
            create_target_cmake += f"target_link_libraries({target} PRIVATE {' '.join(privatelinks)})\n"
        if len(publiclinks) > 0:
            create_target_cmake += f"target_link_libraries({target} PUBLIC {' '.join(publiclinks)})\n"

        if len(filegroups["cmake"]) > 0:
            for f in filegroups["cmake"]:
                create_target_cmake += f"include({f})" + "\n"

        if len(filegroups["h"]) > 0:
            includedirs: Set[pathlib.Path] = set()
            for f in filegroups["h"]:
                includedirs.add(f.parent)
            for d in includedirs:
                create_target_cmake += (
                    f'target_include_directories({target} PRIVATE "{d}")' + "\n"
                )

        if len(filegroups["pidl"]) > 0:
            for f in filegroups["pidl"]:
                create_target_cmake += "use_stencil()\n"
                create_target_cmake += f'add_stencil_library(TARGET {target}_stencil IDLS "{f}")' + "\n"
                create_target_cmake += f'target_link_libraries({target} PUBLIC {target}_stencil)\n'

        if len(filegroups["ly"]) > 0:
            if sys.platform == "win32":
                create_target_cmake += (
                    f'set(WINFLEXBISON_DIR "{externaltools.GetWinFlexBison()}"" CACHE PATH "Win Flex Bison path")'
                    + "\n"
                )
            for f in filegroups["ly"]:
                create_target_cmake += f'target_add_lexyacc({target} "{f}")' + "\n"

        if resources:
            create_target_cmake += "use_stencil()\n"
            for grp, respaths in self._GetResources(srcdir, resources).items():
                respathjoined = '"\n"'.join([p.as_posix() for p in respaths])
                create_target_cmake += (
                    f'target_add_resource({target} {grp} "{respathjoined}")' + "\n"
                )

        if sectype == Type.test:
            create_target_cmake += f"add_test(NAME {target} COMMAND {target})" + "\n"
            create_target_cmake += f"use_catch2({target})" + "\n"
            create_target_cmake += f"install(TARGETS {target} COMPONENT tests)" + "\n"
        elif manifest:
            create_target_cmake += (
                f"install(TARGETS {target} COMPONENT {target})" + "\n"
            )
            create_target_cmake += f"create_app_package({target} {manifest})" + "\n"
        else:
            create_target_cmake += (
                f"install(TARGETS {target} COMPONENT binaries)" + "\n"
            )

        cmakecontents.append(
            f"""
set({fname}_REQUESTED_PACKAGES {" ".join(str(p) for p in packages)})
set({fname}_PLATFORMS {" ".join(platforms)})
list(APPEND VCPKG_REQUESTED_PACKAGES ${{{fname}_REQUESTED_PACKAGES}})

macro(create_target_{fname})
    set({fname}_APPLICABLE TRUE)
    {create_target_cmake}
endmacro(create_target_{fname})

macro(init_target_{fname})
    set({fname}_APPLICABLE FALSE)
    is_packages_applicable({fname}_PACKAGES_APPLICABLE ${{{fname}_REQUESTED_PACKAGES}})
    is_platform_applicable({fname}_PLATFORM_APPLICABLE ${{{fname}_PLATFORMS}})
    if ({fname}_PACKAGES_APPLICABLE AND {fname}_PLATFORM_APPLICABLE)
        create_target_{fname}()
    else()
        message(STATUS "Target {target} not applicable: Packages: ${{{fname}_PACKAGES_APPLICABLE}} Platform: ${{{fname}_PLATFORM_APPLICABLE}}")
        message(STATUS "${{{fname}_PACKAGES_APPLICABLE_FAILED}}")
        add_library({target} INTERFACE)
    endif()
endmacro(init_target_{fname})
"""
        )

        # if sys.platform == "win32":
        #        cmakecontents.append("target_link_libraries({target} PRIVATE WindowsApp.lib rpcrt4.lib onecoreuap.lib kernel32.lib)")
        #        cmakecontents.append("set_target_properties({target} PROPERTIES VS_GLOBAL_MinimalCoreWin true)")
        # if type == "WebViewApp":
        #    cmakecontents.append("include(GenerateExportHeader)")
        #    cmakecontents.append("generate_export_header(avid.lite)")
        #    cmakecontents.append("target_include_directories({target} PRIVATE ${{CMAKE_CURRENT_BINARY_DIR}})")
        #   cmakecontents.append("set(CMAKE_POSITION_INDEPENDENT_CODE ON)")

        newcontents = "\n".join(cmakecontents).replace("\\", "/") + "\n"
        cmakefname = outdir / f"{fname}.cmake"
        if (not cmakefname.exists()) or cmakefname.read_text() != newcontents:
            cmakefname.write_text(newcontents)
        return cmakefname

    def _sections(self):
        boolmap = {"true": True, "false": False, "y": True, "n": False, "1": True,
                   "0": False, "yes": True, "no": False, "t": True, "f": False}
        return [s for s in self._config.sections() if boolmap[str(self._config[s].get('Enabled',  'True')).strip().lower()]]

    def _GenerateCMake(self, outdir: pathlib.Path) -> pathlib.Path:
        # Collect all packages
        # Init all packages
        # Topologically sort targets
        # Create All targets
        # Link all targets
        quicksection = None
        newcontents: List[str] = []
        if "quick" in self._sections():
            quicksection = self._config["quick"]
            if "VcpkgBuildCacheVersion" in quicksection:
                newcontents.append(f'set(VCPKG_BUILD_CACHE_VERSION "{quicksection["VcpkgBuildCacheVersion"]}")')
            if "VcpkgCommit" in quicksection:
                newcontents.append(f'set(VCPKG_COMMIT "{quicksection["VcpkgCommit"]}")')
            self._config.remove_section("quick")

        for s in self._sections():
            self._GenerateCMakeForSection(s, outdir)
        newcontents.append("vcpkg_init()")
        for s in self._sections():
            _target, fname = self._GetSectionNames(s)
            newcontents.append(f"include({outdir}/{fname}.cmake)")
        newcontents.append("vcpkg_install(${VCPKG_REQUESTED_PACKAGES})")
        newcontents.append("vcpkg_export()")

        targetgraph: Dict[str, Set[str]] = {}
        for s in self._sections():
            targetgraph[s] = set(filter(None, [lnk.split(':')[0] for lnk in self._config[s].get("Link", "").split(",")]))

        for s in list(graphlib.TopologicalSorter(targetgraph).static_order()):
            _target, fname = self._GetSectionNames(s)
            newcontents.append(f"init_target_{fname}()")

        newcontentsstr = "\n".join(newcontents).replace("\\", "/") + "\n"
        cmakefname = outdir / "Quick.cmake"

        if (not cmakefname.exists()) or cmakefname.read_text() != newcontentsstr:
            cmakefname.write_text(newcontentsstr)
        return cmakefname

    def Generate(self, mode: str, outdir: pathlib.Path):
        if mode == "cmake":
            return self._GenerateCMake(outdir)
        else:
            raise Exception(f"Unknown Quick Config generation mode = {mode}")

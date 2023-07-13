#!/usr/bin/python3
import hashlib
import os
import pathlib
from typing import Optional

from . import configenv
from . import quick
from . import sync


class BuildEnv:
    def __init__(self, reporoot: pathlib.Path):
        if not reporoot:
            raise Exception(f"Invalid reporoot: {reporoot}")
        self._reporoot: pathlib.Path = reporoot
        self._configenv = configenv.ConfigEnv(reporoot.as_posix())

    def SyncBuildFiles(self):
        curfiledir = pathlib.Path(__file__).parent
        synclist = {
            "cmake/BuildEnv.cmake": None,
            "include/CommonMacros.h": None,
            "include/TestUtils.h": None,
            "mingw_download.ps1": None,
            ".flake8": None,
            ".pylintrc": None,
            "clang-format": ".clang-format",
            "gitignore": ".gitignore",
            "gitattributes": ".gitattributes"
        }

        tgtfiles: dict[str, pathlib.Path] = {}
        rootleveldirs: list[pathlib.Path] = list([d for d in self._reporoot.iterdir() if d.is_dir()
                                                 and d.name != ".git"]) + [pathlib.Path(".")]
        for d in rootleveldirs:
            scandir = self._reporoot / d
            if not scandir.exists():
                continue
            for f in scandir.iterdir():
                if f.is_file():
                    tgtfiles[f.as_posix()] = f
                    tgtfiles[f.name] = f

        for k, v in synclist.items():
            srcf = curfiledir / k
            vname = pathlib.Path(v or k).name
            if vname in tgtfiles:
                sync.Syncer.SyncFiles(srcf, tgtfiles[vname])

        file_quickdecl = os.path.join(self._reporoot, "quickload.config.ini")
        if os.path.exists(file_quickdecl):
            quickbuild = quick.Quick(self._reporoot)
            quickbuild.LoadConfig(pathlib.Path(file_quickdecl))
            quickbuild.VerifyLoaderCMake()

    @ staticmethod
    def FindGitRoot(reporoot: pathlib.Path):
        curdir: pathlib.Path = reporoot
        while(curdir.absolute() != curdir.parent.absolute()):
            if (curdir / ".git").exists():
                return curdir
            curdir = curdir.parent
        return reporoot

    @ staticmethod
    def GetProjectName(reporoot: pathlib.Path) -> str:
        projectname = reporoot.relative_to(BuildEnv.FindGitRoot(reporoot).parent).as_posix()
        projectname = projectname.replace("/", "_")
        return projectname

    def GetBinPath(self) -> pathlib.Path:
        return pathlib.Path(self._configenv.GetConfigPath("DEVEL_BINPATH", make=True))

    def GetTMPDownloadPath(self):
        return self._configenv.GetConfigPath("DEVEL_BINPATH", make=True)

    def ValidateDirectory(self, path: pathlib.Path):
        path.mkdir(exist_ok=True)
        return path

    def GetUniqueName(self, sdir: Optional[pathlib.Path] = None) -> str:
        srcdir: pathlib.Path = sdir or self._reporoot
        hashstr: str = hashlib.md5(srcdir.as_posix().lower().encode('utf-8')).hexdigest()[0:8]
        return f"{srcdir.name}_{hashstr}"

    def GetBaseBuildDir(self) -> pathlib.Path:
        return self._configenv.GetConfigPath("DEVEL_BUILDPATH", make=True) / self.GetUniqueName()

    def GetBuildScriptGenDir(self):
        return self.ValidateDirectory(self.GetBaseBuildDir() / "cmake")

    def GetBuildDir(self, mode: str, arch: str):
        return self.ValidateDirectory(self.GetBaseBuildDir() / f"{mode}_{arch}")

    def GetInstallDir(self, mode: str, arch: str):
        return self.ValidateDirectory(self.GetBaseBuildDir() / "install" / mode / arch)

    def GetPackagesDir(self):
        return self.ValidateDirectory(self.GetBaseBuildDir() / "packages")

    def GetVcpkgRoot(self):
        return self._configenv.GetConfigPath("VCPKG_ROOT", make=True)

    def GetNpmRoot(self,):
        return self._configenv.GetConfigPath("NPM_BUILD_ROOT", make=True)

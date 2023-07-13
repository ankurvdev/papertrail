import configparser
import datetime
import fnmatch
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import threading
from typing import Any, Dict, Optional

import buildenv
import externaltools
import generator


def RunCommand(command: list[str], *args: Any, **kargs: Any):
    print("Command: ", ' '.join(command))
    subprocess.check_call(command, *args, **kargs)


class ExecThread(threading.Thread):
    Sem = threading.BoundedSemaphore(2)
    Error = None

    def __init__(self, action: str, logfilename: Optional[str], cwd: pathlib.Path, command: list[str]):
        threading.Thread.__init__(self)
        self.logfile: Optional[pathlib.Path] = (cwd / logfilename) if logfilename else None
        self.action: str = action
        # self.fhandle = None
        # self.process = None
        self.command = command
        self.cwd: pathlib.Path = cwd

    def run(self):
        try:
            print("Executing: ", self.action, "Logfile: ", self.logfile, "Directory", self.cwd)
            command = self.command
            print("Command: ", ' '.join(command))
            rslt = subprocess.run(command, cwd=self.cwd, check=False)
            if rslt.returncode != 0:
                ExecThread.Error = rslt
        except subprocess.CalledProcessError as ex:
            ExecThread.Error = ex
            # raise Exception(rslt)


def ExecAndLog(action: str, command: list[Any], logfilename: pathlib.Path | str | None = None,
               cwd: pathlib.Path = pathlib.Path(), **kargs: Any):
    command = list([str(arg) for arg in command])
    print(f"Executing: {action}, logfile = {logfilename}, cwd = {cwd}")
    print(" ".join(command))
    rslt = subprocess.run(command, cwd=cwd, env=(os.environ.copy() | externaltools.GetVSVars()),
                          capture_output=True, text=True, check=False, **kargs)
    if rslt.returncode != 0:
        print(rslt.stdout)
        print(rslt.stderr)
        raise Exception(f"Failed Command : {' '.join(command)}")
    # ExecThread.Sem.acquire()
    # try:
    #    thrd = ExecThread(action, logfilename, cwd, command, vsdevshellarch)
    #    # thrd.start()
    #    thrd.run()
    # finally:
    #    ExecThread.Sem.release()
    # if ExecThread.Error:
    # raise Exception(ExecThread.Error)


class ActionRequest:
    configs: list[str]
    archs: list[str]


class Handler:
    def __init__(self, system: str, srcdir: pathlib.Path, builddir: Optional[pathlib.Path]):
        self.system: str = system
        self._srcdir: pathlib.Path = pathlib.Path(srcdir).absolute()
        self._builddir: pathlib.Path = builddir or pathlib.Path(buildenv.BuildEnv(self.GetSourceDir()).GetBaseBuildDir())

    def GetSourceDir(self) -> pathlib.Path:
        return pathlib.Path(self._srcdir)

    def GetBuildRootDir(self) -> pathlib.Path:
        self._builddir.mkdir(exist_ok=True, parents=True)
        return self._builddir

    def PreGenerate(self, request: ActionRequest) -> None:
        pass

    def Generate(self, request: ActionRequest, extra_args: list[str]) -> None:
        pass

    def Format(self, request: ActionRequest) -> None:
        pass

    def Open(self, request: ActionRequest) -> None:
        pass

    def Build(self, request: ActionRequest) -> None:
        pass

    def Pack(self, request: ActionRequest) -> None:
        pass

    def GetPermissionMap(self) -> Dict[str, str]:
        return {}

    def GenerateBuildDirForManifest(self, _name: str, configfile: pathlib.Path, gendir: pathlib.Path, bindings: dict[str, Any] | None = None):
        config = self.LoadConfigAsDictionary(configfile)
        # outdir = os.path.join(self._buildenv.GetBaseBuildDir(), 'android_package')
        srcdir = configfile.parent
        mtime_ns = max(os.stat(__file__).st_mtime_ns, configfile.stat().st_mtime_ns)
        gen = generator.Generator(srcdir, gendir, mtime_ns=mtime_ns)
        appclass = config["application"]["class"]
        # config["build"].setdefault("MajorVersion", 0)
        # config["build"].setdefault("MinorVersion", 0)
        now = datetime.datetime.now()
        versiondate = f"{now.strftime('%y%j')}{int(now.hour/3)}"
        if int(versiondate[1:]) > 965535:
            raise Exception("Version date too long. UWPs wont like that")
        config["build"].setdefault("VersionDate", versiondate)
        majorversion = int(config["build"]["majorversion"])
        minorversion = int(config["build"]["minorversion"])
        if minorversion > 10000 or majorversion > 10000:
            raise Exception("version exceeded limit 10000")
        versiondate = config["build"]["VersionDate"]
        config["build"].setdefault("VersionFull", f"{majorversion}.{minorversion}.{versiondate[1:]}.0")
        config["build"].setdefault("VersionCode", f"{versiondate}")
        systmpldir = pathlib.Path(__file__).parent / "templates" / self.system
        tmpldir = systmpldir / appclass
        if not tmpldir.is_dir():
            raise Exception(f"Cannot find templates for Class : {appclass} : dir : {tmpldir}")
        gen.LoadDictionary(config)
        gen.LoadDictionary(bindings or {})
        cmakeexe = externaltools.GetCMAKE()
        cmakeversion = subprocess.check_output([cmakeexe, "--version"]).decode().splitlines()[0].split(' ')[2].split("-")[0]
        gen.LoadDictionary({
            "BuildDir": gendir.as_posix(),
            "ninjabin": externaltools.GetNinja().as_posix(),
            "cmakebin": cmakeexe.as_posix(),
            "pythonbin": pathlib.Path(sys.executable).as_posix(),
            "cmakebuildtoolsdir": pathlib.Path(__file__).parent.as_posix(),
            "cmakesourcedir": self.GetSourceDir().as_posix(),
            "cmakeversion": cmakeversion})
        gen.GenerateFromTemplateDirectory(tmpldir)
        gen.GenerateImagesForManifest(self.LoadConfigAsDictionary(systmpldir / "images.ini"))
        gen.GenerateImagesForManifest(self.LoadConfigAsDictionary(systmpldir / f"{appclass}.images.ini"))
        return gen, config

    def GetAppManifests(self) -> Dict[str, pathlib.Path]:
        manifests: Dict[str, pathlib.Path] = {}
        quickcfgfiles = list(pathlib.Path(self.GetSourceDir()).glob('*config.ini'))
        if len(quickcfgfiles) == 0:
            for f in self.GetSourceDir().rglob('*.manifest.ini'):
                manifests[os.path.basename(f)] = f
            return manifests
        quickcfg = configparser.ConfigParser()
        for quickcfgfile in quickcfgfiles:
            quickcfg.read(quickcfgfile)
            for sec in quickcfg.sections():
                quicksec = quickcfg[sec]
                if "appmanifest" not in quicksec.keys():
                    continue
                configfile = self.GetSourceDir() / quicksec["appmanifest"]
                config = self.LoadConfigAsDictionary(configfile)
                if self.system not in config:
                    continue
                manifests[sec] = configfile
        return manifests

    def LoadConfigAsDictionary(self, f: pathlib.Path) -> dict[str, Any]:
        config = configparser.ConfigParser()
        if f.exists():
            config.read(str(f))
        return {s.lower(): dict(config.items(s)) for s in config.sections()}

    def rmtree(self, bdir):
        def onerror(func, path, exc_info):
            import stat
            # Is the error an access error?
            if not os.access(path, os.W_OK):
                os.chmod(path, stat.S_IWUSR)
                func(path)
            else:
                raise
        shutil.rmtree(bdir, onerror=onerror)


cmakeconfig = {'dbg': 'Debug', 'rel': 'RelWithDebInfo'}


class CMakeHandler(Handler):
    def __init__(self, system: str, srcdir: pathlib.Path, builddir: Optional[pathlib.Path] = None, **kargs):
        super(CMakeHandler, self).__init__(system, srcdir, builddir)
        self.generator_: Optional[str] = "Ninja" if "prefer_ninja" in kargs and kargs["prefer_ninja"] else None
        if self.generator_ == "Nina" and externaltools.GetNinja() is None:
            sys.stderr.write("Ninja requested but not found. Reverting to Default generator\n")
            self.generator_ = None

    def GetGenerator(self):
        return self.generator_

    def GetGeneratorArgs(self, _arch: str, _config: str) -> list[str]:
        if self.generator_ == "Ninja" and os.name == "nt":
            msvccl = externaltools.GetMSVCCompiler().as_posix()
            return [f"-DCMAKE_MAKE_PROGRAM:FILEPATH='{externaltools.GetNinja().as_posix()}'",
                    f"-DCMAKE_CXX_COMPILER:FILEPATH='{msvccl}'",
                    f"-DCMAKE_C_COMPILER:FILEPATH='{msvccl}'",
                    f"-DCMAKE_MT:FILEPATH='{externaltools.DetectVSPath('mt').as_posix()}'",
                    f"-DCMAKE_RC_COMPILER:FILEPATH='{externaltools.DetectVSPath('rc').as_posix()}'"]
        return []

    def GetCMakeGenerateArgs_(self, arch: str, config: str) -> list[str]:
        return [f"-DPython3_EXECUTABLE:FILEPATH='{pathlib.Path(sys.executable).as_posix()}'",
                f"-DQuick_DIR:PATH='{pathlib.Path(__file__).parent / 'cmake'}'",
                f"-DCMAKE_BUILD_TYPE:STR={cmakeconfig[config]}"]

    def GetBuildDir_(self, arch: str, config: str) -> pathlib.Path:
        bdir = self.GetBuildRootDir() / f"{self.system}_{arch}_{config}"
        bdir.mkdir(exist_ok=True)
        return bdir

    def PreGenerate(self, request: ActionRequest) -> None:
        super().PreGenerate(request)
        srcdir = self.GetSourceDir()

        def GenerateVSCodeWorkspaceSettings(arch: str, config: str):
            (srcdir / ".vscode").mkdir(exist_ok=True)
            file = srcdir / ".vscode" / "settings.json"
            origcontentjson: Dict[str, Any] = json.loads(file.open(mode='r').read()) if file.exists() else {}
            newcontentjson: Dict[str, Any] = json.loads(file.open(mode='r').read()) if file.exists() else {}
            newcontentjson["cmake.buildDirectory"] = self.GetBuildDir_(arch, config).as_posix()
            newcontentjson["cmake.configureArgs"] = self.GetCMakeGenerateArgs_(arch, config)
            newcontentjson["python.defaultInterpreterPath"] = pathlib.Path(sys.executable).as_posix()
            if json.dumps(newcontentjson) != json.dumps(origcontentjson):
                file.write_text(json.dumps(newcontentjson))

        def GenerateVSCMakeSettings(arch: str, config: str):
            bdir = self.GetBuildDir_(arch, config)
            file = srcdir / "CMakeSettings.json"
            origcontentjson: Dict[str, Any] = json.loads(file.open(mode='r', encoding='utf-8-sig').read()) if file.exists() else {}
            newcontentjson: Dict[str, Any] = json.loads(file.open(mode='r', encoding='utf-8-sig').read()) if file.exists() else {}
            newcontentjson.setdefault("configurations", [])
            configname = arch + "-" + config
            sobj = next((entry for entry in newcontentjson["configurations"] if entry.get("name", "") == configname), None)
            obj = sobj or {}
            obj["name"] = configname
            obj["generator"] = "Ninja"  # Only ninja is supported self.GetGenerator()
            obj["configurationType"] = cmakeconfig[config]
            obj["inheritEnvironments"] = ["msvc_" + arch + "_" + arch]
            obj["buildRoot"] = str(bdir)
            obj["installRoot"] = str(bdir / "install")
            obj["cmakeCommandArgs"] = " ".join(self.GetCMakeGenerateArgs_(arch, config))

            if sobj is None:
                newcontentjson["configurations"].append(obj)
            if json.dumps(newcontentjson) != json.dumps(origcontentjson):
                with file.open(mode='w', encoding='utf-8-sig') as f:
                    f.write(json.dumps(newcontentjson))

        for arch in request.archs:
            for config in request.configs:
                GenerateVSCodeWorkspaceSettings(arch, config)
                GenerateVSCMakeSettings(arch, config)

    def Generate(self, request: ActionRequest, extra_args: list[str]) -> None:
        for arch in request.archs:
            for config in request.configs:
                command = [externaltools.GetCMAKE().as_posix()]
                cmakegen = self.GetGenerator()
                if cmakegen:
                    command.extend(["-G", cmakegen])
                command = command + self.GetCMakeGenerateArgs_(arch, config) + self.GetGeneratorArgs(arch, config) + extra_args
                command.append(str(self.GetSourceDir()))
                ExecAndLog("Generate", command, cwd=self.GetBuildDir_(arch, config))

    def Open(self, request: ActionRequest) -> None:
        if sys.platform == "win32":
            subprocess.check_call(["start", "devenv", self.GetSourceDir()], shell=True)
            # return ExecAndLog("Open", None,self.GetBuildDir_(arch, config), [externaltools.GetCMAKE(), "--open", "."])
        else:
            return ExecAndLog("Open", None, self.GetSourceDir(), ["code", self.GetSourceDir()])

    def Format(self, request: ActionRequest) -> None:
        subprocess.check_call([
            sys.executable,
            (pathlib.Path(__file__).parent/"Format.cmake/git-clang-format.py").as_posix(),
            f"--binary={externaltools.GetClangFormat().as_posix()}", "--extensions=cpp,h,cxx"], cwd=self.GetSourceDir())

    def Build(self, request: ActionRequest) -> None:
        for arch in request.archs:
            for config in request.configs:
                command = [externaltools.GetCMAKE().as_posix(), "--build", ".", "-j8", "--config", cmakeconfig[config]]
                ExecAndLog("Build", command, "cmakebuild.txt", self.GetBuildDir_(arch, config))

    def Package(self, request: ActionRequest) -> None:
        for arch in request.archs:
            for config in request.configs:
                command = [externaltools.GetCMAKE().as_posix(), "--build", ".", "--target", "package"]
                ExecAndLog("Build", command, "cmakepackage.txt", self.GetBuildDir_(arch, config))

        for arch in request.archs:
            for config in request.configs:
                for fp in os.scandir(self.GetBuildDir_(arch, config)):
                    if fnmatch.fnmatch(fp.name, '*.zip'):
                        m = re.search(r'(.*)_(.*)-([0-9\.]+)-(.*)-(.*)\.zip', fp.name)
                        if m:
                            garchroot = self.GetBuildRootDir() / m.group(5) / arch
                            os.makedirs(garchroot, exist_ok=True)
                            shutil.unpack_archive(fp.path, extract_dir=garchroot)
                            if (garchroot / "manifestpath.txt").exists():
                                shutil.move((garchroot / "manifestpath.txt"),
                                            self.GetBuildRootDir() / m.group(5) / "manifestpath.txt")
        self.PackageAllArchs(request)

    def PackageAllArchs(self, request: ActionRequest):
        pass

    def Test(self, request: ActionRequest):
        for arch in request.archs:
            for config in request.configs:
                ExecAndLog("Test", [externaltools.GetCTEST().as_posix(), "-C", cmakeconfig[config],
                           "--verbose"], "ctest.txt", self.GetBuildDir_(arch, config))

    def Clean(self, request: ActionRequest):
        for arch in request.archs:
            for config in request.configs:
                bdir = self.GetBuildDir_(arch, config)
                if os.path.isdir(bdir):
                    self.rmtree(bdir)

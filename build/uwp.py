import pathlib
import shutil
import subprocess
import sys
from typing import Optional

import cmake
import externaltools

# import generator


class ExeHandler(cmake.CMakeHandler):
    def __init__(self, srcdir: pathlib.Path, builddir: Optional[pathlib.Path] = None, **kargs):
        super(ExeHandler, self).__init__("exe", srcdir, builddir, **kargs)

    def GetGenerator(self):
        if sys.platform == "win32" and shutil.which('make') is not None:
            return "Unix Makefiles"
        return self.generator_

    def GetGeneratorArgs(self, arch: str, config: str) -> list[str]:
        if self.generator_ is not None and "Visual" in self.generator_:
            ArchMapping = {"x86": "Win32"}
            archstr = ArchMapping.get(arch, None) or arch
            return ["-T", "host=x64", "-A", archstr]
        return super(ExeHandler, self).GetGeneratorArgs(arch, config)

    def PackageAllArchs(self, request: cmake.ActionRequest):
        return
        """
        for fp in os.scandir(packageroot):
            d = fp.path
            manifest = None
            if os.path.exists(os.path.join(d, "manifestpath.txt")):
                with open(os.path.join(d, "manifestpath.txt")) as fd:
                    manifest = fd.read()
            if not manifest:
                continue
            manifest = manifest if os.path.isabs(manifest) else os.path.join(self._reporoot, manifest)
            if not os.path.exists(manifest):
                raise Exception("cannot find manifest", manifest)
            for arch in archs:
                d = os.path.join(fp.path, arch)
                config = configparser.ConfigParser()
                config.read(manifest)
                appxdir = d
                os.makedirs(os.path.expanduser(appxdir), exist_ok=True)
                g = generator.Generator(config, appxdir, os.path.dirname(manifest))
                g.GenerateFiles({"AppxManifest.xml": Win32AppxManifestTemplate})
                g.GenerateImagesFromSpec("Icon", IMAGES)
                cert = os.path.join(os.path.dirname(manifest), "cert.pfx")
                if not os.path.exists(cert):
                    raise Exception("cannot find cert:", cert)
                packfile = os.path.join(packageroot,  config["Microsoft"]["Name"] + "_" + arch + ".msix")
                if os.path.exists(packfile):
                    os.unlink(packfile)
                cmake.RunCommand(["makeappx", "pack", "/o", "/p", packfile, "/d", appxdir], vsdevshellarch=arch, cwd=appxdir)
                cmake.RunCommand(["signtool", "sign", "/fd", "SHA256", "/f", cert, "/p",
                                 "<password>", packfile], vsdevshellarch=arch, cwd=appxdir)"""


class UWPCMakeHandler(cmake.CMakeHandler):
    def __init__(self, srcdir: pathlib.Path, builddir: Optional[pathlib.Path] = None, **kargs):
        super(UWPCMakeHandler, self).__init__("uwp", srcdir, builddir, **kargs)

    def GetGeneratorArgs(self, arch: str, _config: str) -> list[str]:
        ArchMapping = {"x86": "Win32"}
        archstr = ArchMapping.get(arch, None) or arch
        return ["-DCMAKE_SYSTEM_NAME=WindowsStore", "-DCMAKE_SYSTEM_VERSION=10.0", "-T", "host=x64", "-A", archstr]


class UWPHandler(cmake.Handler):
    def __init__(self, srcdir: pathlib.Path, builddir: Optional[pathlib.Path] = None, **kargs):
        super(UWPHandler, self).__init__("uwp", srcdir, builddir, **kargs)
        self._rootbuilddir = (builddir or self.GetBuildRootDir()) / "uwp"
        self._pregendir = self._rootbuilddir / "app"
        self._basebuilddir = self._rootbuilddir / "build"
        self.cmakehandlers: dict[str, UWPCMakeHandler] = {}

    def GetPermissionMap(self):
        return {
            "internet": '<Capability Name="internetClient" /><Capability Name="internetClientServer"/>',
            "bluetooth": '<DeviceCapability Name="bluetooth"/>',
            "ble-server": '',
            "location": '<DeviceCapability Name="location"/>',
            "gyroscope": '',
            "usb": '<DeviceCapability Name="lowLevel"/>',
            "in-app-purchases": '',
            "extended-execution": ''  # TODO
        }

    def PreGenerate(self, request: cmake.ActionRequest) -> None:
        for name, configfile in self.GetAppManifests().items():
            appsrcdir = self._pregendir / name
            self.cmakehandlers[name] = UWPCMakeHandler(self.GetSourceDir(), self._rootbuilddir)
            self.GenerateBuildDirForManifest(name, configfile, appsrcdir)
            cert = appsrcdir / f"{name}.pfx"
            winmd = appsrcdir / "App.winmd"
            shutil.copyfile(configfile.parent / "cert.pfx", cert)
            shutil.copyfile(pathlib.Path(__file__).parent / "templates" / "uwp" / "App.winmd", winmd)

    def Generate(self, request: cmake.ActionRequest, extra_args: list[str]) -> None:
        for _name, handler in self.cmakehandlers.items():
            handler.Generate(request, extra_args)

    def Format(self, request: cmake.ActionRequest) -> None:
        for _name, handler in self.cmakehandlers.items():
            handler.Format(request)

    def Open(self, request: cmake.ActionRequest) -> None:
        for _name, handler in self.cmakehandlers.items():
            handler.Open(request)

    def Build(self, request: cmake.ActionRequest) -> None:
        for name, handler in self.cmakehandlers.items():
            handler.Build(request)
            configmapping = {
                "dbg": "Debug",
                "rel": "Release"
            }
            archmapping = {
                "x86": "x86",
                "x64": "x64",
                "arm": "ARM",
                "arm64": "arm64",
            }
            subprocess.check_call([str(externaltools.DetectVSPath('msbuild')), "-t:restore",
                                  "-p:RestorePackagesConfig=true"], cwd=(self._pregendir / name))
            for config in request.configs:
                for arch in request.archs:
                    subprocess.check_call([str(externaltools.DetectVSPath('msbuild')),
                                           "/p:Configuration=" + configmapping[config],
                                           "/p:Platform=" + archmapping[arch],
                                           "App.sln"
                                           ], cwd=(self._pregendir / name))

    def Clean(self, _request: cmake.ActionRequest) -> None:
        if self._rootbuilddir.exists():
            self.rmtree(self._rootbuilddir)

    def Package(self, request: cmake.ActionRequest):
        self.PreGenerate(request)
        outdir = self._rootbuilddir
        archmapping = {
            "x86": "x86",
            "x64": "x64",
            "arm": "ARM",
            "arm64": "arm64",
        }
        for name, handler in self.cmakehandlers.items():
            handler.Build(request)
            subprocess.check_call([str(externaltools.DetectVSPath('msbuild')), "-t:restore",
                                  "-p:RestorePackagesConfig=true"], cwd=(self._pregendir / name))
            subprocess.check_call([str(externaltools.DetectVSPath('msbuild')),
                                   "/p:Configuration=Release",
                                   "/p:AppxBundlePlatforms=" + "|".join([archmapping[a] for a in request.archs]),
                                   "/p:AppxPackageDir=MyPackages",
                                   "/p:AppxBundle=Always",
                                   "/p:UapAppxPackageBuildMode=StoreUpload",
                                   "/p:AppxPackageSigningEnabled=true",
                                   "/p:PackageCertificateKeyFile=" + f"{name}.pfx",
                                   "App.sln"
                                   ], cwd=(self._pregendir / name))
            appblddir = self._basebuilddir / name
            appsrcdir = self._pregendir / name
            cert = appsrcdir / f"{name}.pfx"
            for msix in list(appblddir.rglob(f"{name}UWP*.msix")):
                command = [str(externaltools.DetectVSPath('signtool')), "sign", "/fd", "SHA256", "/p",
                           "<password>", "/f", str(cert), str(msix)]
                print(" ".join(command))
                subprocess.check_call(command, cwd=outdir)
                # subprocess.check_call([self.MAKEAPPX_BINARY, "pack", "/o", "/p", packfile, "/d", os.path.dirname(f)], cwd=appxdir)
                shutil.copyfile(msix, outdir)

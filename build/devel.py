#!/usr/bin/python3
import argparse
import datetime
import os
import os.path
import pathlib
import subprocess
import sys

import android
import buildenv
import cmake
import configenv
import externaltools
import packager
import quick
import svelte
import uwp
import vcpkg
import wasm


def normalize_path(fpath: str):
    return pathlib.Path(os.path.expandvars(os.path.expanduser(str(fpath))))


class Devel:
    def __init__(self, reporoot: str):
        self._buildenv = buildenv.BuildEnv(normalize_path(reporoot))

    def InitRepo(self):
        pass


Systems = ["uwp", "and", "andcli", "exe", "wasm"]
Archs = ["x86", "x64", "arm", "arm64"]
Configs = ["rel", "dbg"]
Actions = ["clean", "gen", "build", "pack", "test", "open", "format"]


def AcquireTool(args, _extra: list[str]):
    return externaltools.AcquireTool(args.tools)


def InitRepo(args, _extra: list[str]):
    return Devel(normalize_path(args.reporoot)).InitRepo()


def ConfigSubCommandHandler(args, _extra: list[str]):
    print(configenv.ConfigEnv(normalize_path(args.reporoot)).GetConfigStr(args.queryparam))


def QuickSubCommandHandler(args, _extra: list[str]):
    configs = [normalize_path(configpath) for configpath in args.config]
    quick.Quick(normalize_path(args.reporoot)).LoadConfig(configs).Generate(args.outtype, normalize_path(args.out))


def ActionSubCommandHandler(args, a, _extraargs: list[str]):
    def ParseArgs(switches):
        groups = {"systems": [], "archs": [], "configs": [], "actions": []}
        for s in switches:
            if s in Archs:
                groups["archs"].append(s)
            if s in Systems:
                groups["systems"].append(s)
            if s in Configs:
                groups["configs"].append(s)
            if s in Actions:
                groups["actions"].append(s)

        if len(groups["actions"]) == 0:
            groups["actions"].append("gen")
        if len(groups["archs"]) == 0:
            groups["archs"].append("x64")
        if len(groups["configs"]) == 0:
            groups["configs"].append("dbg")
        if len(groups["systems"]) == 0:
            groups["systems"].append("exe")
        return groups
    print(a, args)
    opts: dict[str, object] = {}
    if args.prefer_ninja:
        opts["prefer_ninja"] = True

    switches = [a] + args.switches
    if len(switches) > 0:
        if not args.nosync:
            buildenv.BuildEnv(normalize_path(args.reporoot)).SyncBuildFiles()
        groups = ParseArgs(switches)
        starttime = datetime.datetime.now()

        for s in groups["systems"]:
            handler: cmake.Handler = uwp.ExeHandler(normalize_path(args.reporoot), **opts)
            if s == "uwp":
                handler = uwp.UWPHandler(normalize_path(args.reporoot), **opts)
            if s == "exe":
                pass
            if s == "wasm":
                handler = wasm.WasmHandler(normalize_path(args.reporoot), **opts)
            if s == "and":
                handler = android.AndroidAppHandler(normalize_path(args.reporoot), **opts)
            if s == "andcli":
                handler = android.AndroidCLIHandler(normalize_path(args.reporoot), **opts)
            tasks = groups["actions"]
            if "test" in tasks:
                tasks.extend(["build"])
            if "pack" in tasks:
                tasks.extend(["gen"])
            if "build" in tasks:
                tasks.extend(["gen"])
            if "format" in tasks:
                tasks.extend(["gen"])
            if "open" in tasks:
                tasks.extend(["gen"])
            tasks = list(set(tasks))
            req = cmake.ActionRequest()
            req.archs = groups["archs"]
            req.configs = groups["configs"]
            if "clean" in tasks:
                handler.Clean(req)
            if "gen" in tasks:
                handler.PreGenerate(req)
                handler.Generate(req, _extraargs)
            if "format" in tasks:
                handler.Format(req)
            if "build" in tasks:
                handler.Build(req)
            if "open" in tasks:
                handler.Open(req)
            if "pack" in tasks:
                handler.Package(req)
            if "test" in tasks:
                handler.Test(req)

        endtime = datetime.datetime.now()
        print("ElapsedTime:", endtime - starttime)


def CreateActionSubCommand(subparsers, a):
    parser = subparsers.add_parser(a, help="Config Query Set")
    parser.add_argument("switches",
                        type=str,
                        nargs='*',
                        default=[],
                        # choices=(Actions + Systems + Archs + Configs),
                        help="Switches")

    parser.add_argument("--openlog",
                        action='store_true',
                        default=False,
                        help="Open Error logs in notepad")
    parser.set_defaults(func=lambda args, _extraargs: ActionSubCommandHandler(args, a, _extraargs))


def main():

    parser = argparse.ArgumentParser(description="Loading Script")

    parser.add_argument("--reporoot", type=str, default=None, help="Repository")
    parser.add_argument("--nostdin", action='store_true', default=False, help="Do not prompt")
    parser.add_argument("--nosync", action='store_true', default=False, help="Do not prompt")
    parser.add_argument("--prefer-ninja", action='store_true', default=False, help="Prefer Ninja")

    subparsers = parser.add_subparsers(help='sub-command help')
    vcpkg_parser = vcpkg.Vcpkg.CreateArgParser(subparsers.add_parser('vcpkg', help="Vcpkg interactions"))
    vcpkg_parser.set_defaults(func=vcpkg.Vcpkg.ArgHandler)

    npm_parser = svelte.SvelteBuilder.CreateArgParser(subparsers.add_parser('npm', help="NPM interactions"))
    npm_parser.set_defaults(func=svelte.SvelteBuilder.ArgHandler)

    acquire_tool_parser = subparsers.add_parser('acquiretool', help="Acquire Build Tools")
    acquire_tool_parser.add_argument('tools', type=str, default=None, help="Tools to acquire")
    acquire_tool_parser.set_defaults(func=AcquireTool)

    init_repo_parser = subparsers.add_parser('initrepo', help="Initialize Repository")
    init_repo_parser.set_defaults(func=InitRepo)

    config_parser = subparsers.add_parser('config', help="Config Query Set")
    config_parser.add_argument("--queryparam", type=str, default=None, help="Query Value for Parameter")
    config_parser.set_defaults(func=ConfigSubCommandHandler)

    quick_parser = subparsers.add_parser('quick', help="Quick")
    quick_parser.add_argument("config", type=str, nargs='+', default=None, help="Config File")
    quick_parser.add_argument("out", type=str, default=None, help="Output directory")
    quick_parser.add_argument("--outtype", type=str, default=None, help="Type")
    quick_parser.set_defaults(func=QuickSubCommandHandler)

    packager_parser = packager.Packager.CreateArgParser(subparsers.add_parser('packager', help="Packaging"))
    packager_parser.set_defaults(func=packager.Packager.ArgHandler)

    for a in Actions:
        CreateActionSubCommand(subparsers, a)

    args, extra = parser.parse_known_args()
    if not args.nostdin:
        configenv.ConfigEnv.StartMonitor()
    try:
        if os.path.islink(__file__):
            sys.path.append(os.path.join(os.path.dirname(os.readlink(__file__))))
        else:
            sys.path.append(os.path.join(os.path.dirname(__file__)))

        if (sys.prefix == sys.base_prefix and not getattr(sys, 'frozen', False)):
            sys.stderr.write(f"Creating Virtual Environment and Relaunching {sys.argv}\n")
            venv = configenv.ConfigEnv("").GetConfigPath("DEVEL_BUILDPATH", make=True) / "python_venv"
            if not venv.exists():
                subprocess.check_output([sys.executable, "-m", "venv", venv.as_posix()])
            pyexe = venv / "Scripts" / "python.exe" if sys.platform == "win32" else (venv / "bin" / "python3")
            pips = subprocess.check_output([pyexe, "-m", "pip", "list", "--format", "freeze"], text=True).splitlines()
            installedpips = set([pip.split("==")[0] for pip in pips])
            requiredpips = ["PyQt6"]
            installpips = [pip for pip in requiredpips if pip not in installedpips]
            if len(installpips) > 0:
                subprocess.check_output([pyexe, "-m", "pip", "install", "--upgrade", "pip"] + installpips)
            configenv.ConfigEnv.StopMonitor()
            subprocess.run([pyexe] + sys.argv, check=True)
            exit(0)
        sys.stderr.write(f"Prefix: {sys.prefix}\nBase: {sys.base_prefix}\nExe : {sys.executable} {getattr(sys, 'frozen', False)}\n")

        args.func(args, extra)
    finally:
        configenv.ConfigEnv.StopMonitor()


if __name__ == "__main__":
    main()

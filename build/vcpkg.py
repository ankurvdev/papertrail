import argparse
import datetime
import fnmatch
import json
import multiprocessing
import os
import pathlib
import re
import shutil
import subprocess
import sys
from typing import Sequence
import itertools

import configenv
import externaltools
import git


class VcpkgLock:
    def __init__(self):
        self.lock = multiprocessing.Lock()
        self.lock.acquire()

    def __del__(self):
        self.lock.release()

# Test Mode
# Production Mode
# Checkout and build https://github.com/ankurvdev/vcpkg lkg_patched

# Scenario 1
# vcpkg.py --test default
#   * Reuses config:VCPKG_ROOT if environment defined
#   * uses vcpkg.test<date> if no config:VCPKG_ROOT defined
# vcpkg.py ~/build/vcpkg.py --test default --marklkg --commit a267ab118c09f56f3dae96c9a4b3410820ad2f0b
#   * Reuses config:VCPKG_ROOT
#   * does not create a fresh


VCPKG_KNOWN_FAILURES = {
    "osgearth:*-android",
    "osgearth:*-mingw*",
    "osgearth:*-uwp*",
    "curses:*-uwp*",
    "rtlsdr:*-uwp*",
    "rtlsdr:*-emscripten*",
    "libspatialite:*-emscripten*"
}

TEST_PORTS = {
    "qtbase",
    "curses",
    "stencil",
    "rtlsdr",
    "libspatialite",
    "asio",
}

TEST_TRIPLETS = {
    "arm-neon-android",
    "arm64-android",
    "x86-android",
    "x64-android",
    "wasm32-emscripten",
    "x64-windows-static-md",
    "x64-linux"
}


class VcpkgException(Exception):
    pass


class Vcpkg:
    def __init__(self,
                 vcpkg_root: str | None,
                 triplet: str,
                 host_triplet: str | None = None,
                 commit: str | None = None):
        # Try to clone from upstream
        # Add myfork as a remote
        # Do not want to make root for scenario 1
        host_triplet = host_triplet if host_triplet != "auto" else None
        wintriplet = "x64-mingw-static" if sys.platform == "win32" and shutil.which("mingw32-make") else "x64-windows-static-md"
        self.triplet = triplet or (wintriplet if sys.platform == "win32" else "x64-linux")
        self.host_triplet = host_triplet or (wintriplet if sys.platform == "win32" else "x64-linux")
        self.commit = commit
        self.rourl = 'https://github.com/ankurvdev/vcpkg.git'
        self.rwurl = 'git@github.com:ankurvdev/vcpkg'
        self.lock = VcpkgLock()
        self.mode = 'unknown'  # Could be git or buildcache
        self.vcpkg_root = pathlib.Path(vcpkg_root) if vcpkg_root else pathlib.Path()
        self.envvars: dict[str, str] = {"VCPKG_BINARY_SOURCES": "clear"}
        self._git = git.Git(root=self.vcpkg_root)
        self._init_pass_2_done = False
        if sys.platform == "linux" and os.uname().machine == "armv7l":
            self.envvars["VCPKG_FORCE_SYSTEM_BINARIES"] = "1"

    def _HeadHasAllPatches(self):
        for branch in self._get_patch_branches():
            if self._git.cmd(["merge-base", "HEAD", branch]).strip() != self._git.commit_hash(branch):
                sys.stderr.write(f"HEAD does not contain Branch {branch} ")
                return False
        return True

    def _CreateWorkBranchWithAllPatches(self):
        i = 0
        while True:
            workbranch = datetime.datetime.now().strftime('%Y%m%d') + "_" + str(i)
            if workbranch not in self._git.cmd(["branch"]):
                break
            i = i + 1

        self._git.cmd(["fetch", "--all", "--prune"])
        if self.commit is None:
            self._git.cmd(["checkout", "--", "."])
            self._git.cmd(["clean", "-f", "-d"])
            if workbranch not in self._git.cmd(["branch"]):
                self._git.cmd(["checkout", "origin/master", "-b", workbranch])
            self._git.cmd(["checkout", workbranch])
            for b in self._get_patch_branches():
                self._git.cmd(["merge", b])
        else:
            currenthead = self._git.commit_hash("HEAD")
            expectedhead = self._git.commit_hash(self.commit)
            if currenthead != expectedhead:
                # print("Current HEAD:", currenthead, "Does not match:", self.commit, "VCPKG_ROOT = ", self.locate_vcpkg_root())
                self._git.cmd(["checkout", self.commit, "-b", workbranch])
        # Merge in both cases bacause we really dont want to run without these patches.
        # They're usually no-op if already merged
        for b in self._get_patch_branches():
            self._git.cmd(["merge", b])

    def _MakeAllTripletsReleaseOnly(self):
        tripletdir = (self._git.root / "triplets")
        for tripletfile in tripletdir.rglob('*.cmake'):
            sys.stderr.write(f"Patching {tripletfile} for Release only builds\n")
            with tripletfile.open("a") as tripletfd:
                tripletfd.write('set(VCPKG_BUILD_TYPE release)\n')

    def _ValidateRepo(self) -> pathlib.Path:
        vcpkg_root = self.locate_vcpkg_root()

        if self.commit is not None and self._git.commit_hash("HEAD") == self._git.commit_hash(self.commit):
            # Quick validation . Complete
            return vcpkg_root
        self._git.sync()
        if not self._HeadHasAllPatches():
            self._CreateWorkBranchWithAllPatches()
        is_vcpkg_build_type_release_only = os.environ.get('VCPKG_BUILD_TYPE_RELEASE_ONLY', "0")
        boolmap = {"true": True, "false": False, "y": True, "n": False, "1": True,
                   "0": False, "yes": True, "no": False, "t": True, "f": False}
        if boolmap[is_vcpkg_build_type_release_only]:
            self._MakeAllTripletsReleaseOnly()

        if not vcpkg_root:
            raise VcpkgException("Cannot initialize vcpkg exe")
        subprocess.check_output([str(self._vcpkgexe()), "--disable-metrics", "remove", "--outdated", "--recurse"])

        return vcpkg_root

    def _get_patch_branches(self) -> list[str]:
        return self._git.query_branches(r'remotes/((myfork|origin)/ankurv/(.+))$')

    def _get_patch_topics(self) -> list[str]:
        branches: list[str] = []
        for b in self._git.cmd(["branch", "-a"]).splitlines():
            b = b.strip()
            rematch = re.search(r'remotes/((myfork|origin)/ankurv/(.+))$', b)
            if not rematch:
                continue
            branches.append(rematch.group(3))
        return branches

    def _InvokeCmd(self, cmd: list[str], **kargs: object) -> str:
        rslt = subprocess.run(cmd, text=True, check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kargs)
        if rslt.returncode != 0:
            raise VcpkgException("Failed : " + " ".join(cmd))
        return rslt.stdout

    def _vcpkgexe(self):
        path = shutil.which("vcpkg", path=self.locate_vcpkg_root())
        if path is None:
            raise VcpkgException(f"Cannot find vcpkg exe in {self.locate_vcpkg_root()}")
        return os.path.abspath(path)

    def apply_patches(self) -> pathlib.Path:
        return self._ValidateRepo()

    def _clear_logs(self, pkgs: list[str]):
        for p in pkgs:
            for f in self._get_logs(p):
                os.unlink(f)

    def _get_logs(self, name: str) -> list[pathlib.Path]:
        return list((pathlib.Path(self.locate_vcpkg_root()) / "buildtrees" / name).rglob('*.log'))

    def locate_vcpkg_root(self) -> pathlib.Path:
        vcpkg_root = self.vcpkg_root
        if vcpkg_root == pathlib.Path():
            vcpkg_root = pathlib.Path(configenv.ConfigEnv(None).GetConfigPath("VCPKG_ROOT", make=True))
        if not self._init_pass_2_done:
            self._git.set_root(vcpkg_root)
            if not vcpkg_root.is_dir() and not vcpkg_root.is_absolute():
                raise VcpkgException(f"Refusing to initial non-absolute vcpkg-root directory: {vcpkg_root}")
            if not (vcpkg_root / ".git").is_dir():
                if vcpkg_root.exists() and len(list(os.scandir(str(vcpkg_root)))) != 0:
                    raise VcpkgException(f"Refusing to initial non-empy vcpkg-root directory: {vcpkg_root}")
                subprocess.check_call(["git", "clone", self.rourl, vcpkg_root.name], cwd=vcpkg_root.parent)
            if not (vcpkg_root / "scripts/buildsystems/vcpkg.cmake").exists():
                raise VcpkgException(f"Invalid vcpkg_root : {vcpkg_root}")
            self._git.sync()

            self.mode = 'git'
            if shutil.which('vcpkg', path=vcpkg_root) is None:
                if sys.platform == "win32":
                    subprocess.check_call(["bootstrap-vcpkg.bat"], shell=True, cwd=vcpkg_root)
                else:
                    subprocess.check_call(["./bootstrap-vcpkg.sh"], shell=True, cwd=vcpkg_root)
            self._init_pass_2_done = True
        return vcpkg_root

    def _download(self, ports: str | list[str], keep_going=False, editable=False):
        installports: list[str] = []
        envvars: dict[str, str] = self.envvars
        paths: set[pathlib.Path] = set()
        environ = os.environ.copy()
        for port in ports:
            port = ":".join((port + ":" + self.triplet).split(':')[0:2])

            def IsKnownFailure(port: str):
                for knownfailures in VCPKG_KNOWN_FAILURES:
                    if fnmatch.fnmatch(port, knownfailures):
                        return True
                return False
            if not IsKnownFailure(port):
                installports.append(port)
        if "android" in ",".join(installports):
            envvars["ANDROID_NDK_HOME"] = externaltools.GetAndroidNDKRoot().as_posix()
            envvars["ANDROID_SDK_ROOT"] = externaltools.GetAndroidSdkRoot().as_posix()
            paths.add(externaltools.GetJava().parent)
        if "wasm32" in ",".join(installports):
            environ.pop('PKG_CONFIG_LIBDIR', '')  # Interferes with vcpkg
            envvars |= externaltools.GetEmscriptenVars()
            paths |= externaltools.GetEmscriptenPaths()

        self._clear_logs(installports)
        if paths:
            environ = externaltools.AddToPath(paths, environ)
        if envvars:
            for envvarname, envvarval in envvars.items():
                sys.stderr.write(f'{envvarname} = {envvarval}\n')
                environ[envvarname] = envvarval
        command = [self._vcpkgexe(),
                   f"--host-triplet={self.host_triplet}",
                   f"--triplet={self.triplet}",
                   "--disable-metrics",
                   "install"] + installports + [
            "--recurse",
            "--allow-unsupported"]
        if keep_going:
            command.append('--keep-going')
        if editable:  # Dependency ports arent editable
            command.append('--editable')
        sys.stderr.write(f'Command: {" ".join(command)}')
        rslt = subprocess.run(command, env=environ, capture_output=True, cwd=self.locate_vcpkg_root(), check=False)
        if rslt.returncode != 0:
            errors = rslt.stdout.decode()
            errortriplets: list[str] = []
            for line in errors.splitlines():
                m = re.match(r'Error: Building package (.*) failed with: BUILD_FAILED', line)
                if not m:
                    continue
                errortriplets += [m.group(1)]
            for t in errortriplets:
                name, _arch = t.split(':')
                logfiles: list[pathlib.Path] = self._get_logs(name)
                logfiles.sort(key=lambda x: x.stat().st_mtime_ns)
                for p in logfiles:
                    sys.stderr.write(f"\nFailed {t}: Log: {str(p)}\n\n")
                    with p.open('r') as f:
                        shutil.copyfileobj(f, sys.stderr)
            if len(errortriplets) == 0:
                sys.stderr.write(rslt.stdout.decode())
                sys.stderr.write(rslt.stderr.decode())
            raise VcpkgException(f"Error Triplets: {errortriplets}")

    def download(self, ports: str | list[str]):
        self.locate_vcpkg_root()
        if self.mode == 'buildcache':
            print("Skipping Download in Cached Mode")
            return
        self._download(ports)

    def _update_topic_branch(self, tag: str, mastercommit: str, topic: str):
        current_commit = self._git.commit_hash('HEAD')
        mastercommit = self._git.commit_hash(mastercommit)
        oldbranch = f'origin/ankurv/{topic}'
        newbranch = f'{tag}/ankurv/{topic}'
        if self._git.try_get_commit_hash(newbranch) is not None:
            self._git.checkout(newbranch)
        self._git.checkout(self._git.commit_hash('HEAD'))
        self._git.merge_branch(oldbranch)
        self._git.merge_branch(mastercommit, rebase_squash=False)
        self._git.delete_branch(newbranch)
        self._git.create_branch(newbranch, self._git.commit_hash('HEAD'))
        self._git.checkout(current_commit)
        return newbranch

    def test(self, tag: str, ports: list[str], _commit: str = 'HEAD', keep_going=False, editable=False):
        portlist: list[str] = []
        if len(ports) == 0 or ports == ['default']:
            defaultlist = TEST_PORTS
            defaulttriplets = TEST_TRIPLETS
            filterout = "linux" if sys.platform == "win32" else "windows"
            defaulttriplets = list(filter(lambda x: filterout not in x, TEST_TRIPLETS))
            portlist = [":".join(t) for t in itertools.product(defaultlist, defaulttriplets)]
            sys.stderr.write(" ".join(portlist))
        self.vcpkg_root = self.vcpkg_root or pathlib.Path(os.getcwd(), "vcpkg.test" + datetime.datetime.now().strftime('%Y%m%d'))
        self._git.set_root(self.vcpkg_root)
        self.locate_vcpkg_root()
        self._git.sync()
        # Check if we're on some branch or just a orphaned commit
        currentbranch = self._git.branch_name_for_commit('HEAD')
        if currentbranch == 'HEAD':
            currentbranch = self._git.commit_hash(currentbranch)

        lkgcommitbranch = f'{tag}/lkg_commit'
        if self._git.try_get_commit_hash(lkgcommitbranch) is None:
            self._git.create_branch(lkgcommitbranch, currentbranch)
        lkgcommit = self._git.commit_hash(lkgcommitbranch)

        topics = self._get_patch_topics()
        topicbranches = list([self._update_topic_branch(tag, lkgcommit, topic) for topic in topics])
        topicbranches = self._git.query_branches(f'{tag}/ankurv/(.+)$')
        for branch in topicbranches:
            self._git.merge_branch(branch)
        self._download(portlist, keep_going=keep_going, editable=editable)
        for topic in topics:
            newbranch = f'{tag}/ankurv/{topic}'
            self._git.checkout(newbranch)
            self._git.merge_branch(lkgcommit, rebase_squash=True)

        lkgpatchedbranch = f'{tag}/lkg_patched'
        self._git.delete_branch(lkgpatchedbranch)
        self._git.create_branch(lkgpatchedbranch, lkgcommit)
        for topic in topics:
            self._git.merge_branch(f'{tag}/ankurv/{topic}', rebase_squash=False)

        branches = ['lkg_commit', 'lkg_patched'] + list([f'ankurv/{topic}' for topic in topics])
        for branch in branches:
            self._git.delete_branch(branch)
            self._git.create_branch(branch, f'{tag}/{branch}', )

        self._git.cmd(['push', 'origin', '--force', '--set-upstream'] + branches + list([f'{tag}/ankurv/{topic}' for topic in topics]))

    def remove(self, patterns: list[str]):
        ports: list[str] = []
        for p in self.query():
            for pattern in patterns:
                if fnmatch.fnmatch(p, pattern):
                    ports.append(p)
        print("Removing ports", ports)
        subprocess.check_call([self._vcpkgexe(), "--disable-metrics", "remove"] +
                              ports + ["--recurse"], cwd=self.locate_vcpkg_root())

    def clean(self, tripletname: str):
        pass

    def query(self) -> list[str]:
        self.locate_vcpkg_root()
        if self.mode == 'buildcache':
            return []
        listjson = subprocess.check_output([
            str(self._vcpkgexe()),
            "--disable-metrics",
            "list",
            "--x-json"],
            cwd=self.locate_vcpkg_root())
        listobj = json.loads(listjson)
        return listobj.keys()

    @ staticmethod
    def CreateArgParser(parser: argparse.ArgumentParser):
        parser.add_argument("--host-triplet", type=str, default=None, help="Host Triplet")
        parser.add_argument("--triplet", type=str, default=None, help="Triplet")
        parser.add_argument("--root", type=str, default=None, help="Root")
        parser.add_argument("--commit", type=str, default=None, help="Commit")
        parser.add_argument("--apply-patches", action='store_true', default=False, help="Apply patches")
        parser.add_argument("--keep-going", action='store_true', default=False, help="Keep Going")
        parser.add_argument("--editable", action='store_true', default=False, help="Editable")
        parser.add_argument("--download", type=str, default=None, help="Ports")
        parser.add_argument("--test", type=str, default=None, help="Ports")
        parser.add_argument("--marklkg", type=str, default=None, help="Install")
        parser.add_argument("--export", type=str, default=None, help="Ports")
        parser.add_argument("--remove", type=str, default=None, help="Ports to remove")
        parser.add_argument("--list", type=str, default=None, help="Where to list the ports")
        return parser

    @ staticmethod
    def ArgHandler(args: Sequence[str] | None, _extra: list[str]):
        args.root = os.path.expandvars(os.path.expanduser(args.root)) if args.root else None
        vcpkger = Vcpkg(args.root, args.triplet, host_triplet=args.host_triplet, commit=args.commit)

        def portsplit(ports: str | list[str]) -> list[str]:
            if ports is None:
                return []
            if isinstance(ports, str):
                return str(ports).split(",")
            return list(ports)

        if args.apply_patches:
            vcpkger.apply_patches()
            print(vcpkger.locate_vcpkg_root().as_posix())

        if args.remove:
            vcpkger.remove(portsplit(args.remove))

        if args.test:
            vcpkger.test(args.test, portsplit(args.download or "default"), _commit=(
                args.marklkg or 'HEAD'), keep_going=args.keep_going, editable=args.editable)
            return

        if args.download:
            vcpkger.download(portsplit(args.download))

        if args.list is not None:
            if args.list == "-":
                print("\n".join(vcpkger.query()))
            else:
                pathlib.Path(args.list).write_text("\n".join(vcpkger.query()), encoding='utf-8')


if __name__ == "__main__":
    # Use Cases
    # 1. New machine vcpkg_cache : python Vcpkg.py --download "port" --buildcache <name>
    # 2. New machine vcpkg_root : python Vcpkg.py --download "port"
    # 3. Test new build python Vcpkg.py --test
    # 4. Rebase python Vcpkg.py --test --marklkg --commit=<>

    Vcpkg.ArgHandler(Vcpkg.CreateArgParser(argparse.ArgumentParser(description="Loading Script")).parse_args(), [])

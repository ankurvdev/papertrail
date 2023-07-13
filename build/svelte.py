import argparse
import configparser
import datetime
import os
import pathlib
import subprocess

from . import buildenv
from . import externaltools
from . import generator
from . import sync


class SvelteBuilderException(Exception):
    pass


class SvelteBuilder:
    def __init__(self,
                 subdir: pathlib.Path,
                 reporoot: pathlib.Path,
                 buildroot: pathlib.Path | None,
                 out_file_list: pathlib.Path | None):
        self._reporoot = reporoot
        self._buildenv = buildenv.BuildEnv(reporoot)
        if subdir.is_absolute():
            subdir = subdir.relative_to(reporoot)
        self.npm_exe = externaltools.GetNPM()
        self.npx_exe = externaltools.SearchExecutable(self.npm_exe.parent, "npx")
        self.env = os.environ.copy()
        self.env["PATH"] += os.pathsep + self.npm_exe.parent.as_posix()
        self.npm_build_root = pathlib.Path(buildroot or self._buildenv.GetBaseBuildDir() / f'npm_{self._buildenv.GetUniqueName(subdir)}')
        self.npm_build_root.mkdir(exist_ok=True, parents=True)
        self.npm_build_root = self.npm_build_root.absolute()
        self.srcdir = reporoot / subdir
        self.out_file_list = out_file_list
        self.dist = self.npm_build_root / "dist"
        npmsrc = self.npm_build_root / 'src'
        # if not npmsrc.exists():
        #    os.symlink(self.srcdir.absolute().as_posix(), npmsrc.as_posix())
        # if not npmsrc.is_symlink:
        #    raise SvelteBuilderException(f"{npmsrc.as_posix()} is not a symlink")
        try:
            self.syncer = sync.Syncer(npmsrc, self.srcdir)
            self.syncer.SyncWork()
            self.syncer.SyncSource()
        except sync.SyncException:
            pass

    def LoadConfigAsDictionary(self, f: pathlib.Path) -> dict[str, dict[str, str]]:
        config = configparser.ConfigParser()
        if f.exists():
            config.read(str(f))
        return {s.lower(): dict(config.items(s)) for s in config.sections()}

    def generate(self):
        configfile = self.srcdir / 'svelte.config.ini'
        config = self.LoadConfigAsDictionary(configfile)
        mtime_ns = max(os.stat(__file__).st_mtime_ns, configfile.stat().st_mtime_ns)
        gen = generator.Generator(self.srcdir, self.npm_build_root, mtime_ns=mtime_ns)

        # config["build"].setdefault("MajorVersion", 0)
        # config["build"].setdefault("MinorVersion", 0)
        now = datetime.datetime.now()
        versiondate = f"{now.strftime('%y%j')}{int(now.hour/3)}"
        if int(versiondate[1:]) > 965535:
            raise SvelteBuilderException("Version date too long. UWPs wont like that")
        config.setdefault("build", {})
        config.setdefault("npm", {})
        config["build"].setdefault("VersionDate", versiondate)
        # majorversion = int(config["build"]["majorversion"])
        # minorversion = int(config["build"]["minorversion"])
        # if minorversion > 10000 or majorversion > 10000:
        #    raise Exception("version exceeded limit 10000")
        versiondate = config["build"]["VersionDate"]
        # config["build"].setdefault("VersionFull", f"{majorversion}.{minorversion}.{versiondate[1:]}.0")
        # config["build"].setdefault("VersionCode", f"{versiondate}")
        if self._buildenv.GetNpmRoot() is None:
            raise SvelteBuilderException(f"No NPM ROOT specified: {self._buildenv.GetNpmRoot()}")
        config["npm"].setdefault("GlobalPackageRoot", self._buildenv.GetNpmRoot().as_posix())
        config["npm"].setdefault("Executable", self.npm_exe.as_posix())
        packages = filter(len, config["svelte"].get("packages", "").split(","))
        proxies = filter(len, config["svelte"].get("proxies", "").split(","))
        # pylint: disable=C0209
        proxies = ",".join(map(lambda entry: "'{}' : '{}'".format(*entry.split("::")), proxies))
        config["npm"].setdefault("Proxies", proxies)
        config["npm"].setdefault("Packages", "\n".join([f'"{pkgname}" : "latest",' for pkgname in packages]))
        systmpldir = pathlib.Path(__file__).parent / "templates" / 'npm'
        tmpldir = systmpldir / 'svelte'
        if not systmpldir.is_dir():
            raise SvelteBuilderException("Cannot find templates for npm")
        gen.LoadDictionary(config)
        gen.GenerateFromTemplateDirectory(tmpldir)
        gen.GenerateImagesForManifest(self.LoadConfigAsDictionary(systmpldir / "images.ini"))
        self.prebuild()
        self.build()

    def prebuild(self):
        (self.npm_build_root / "build").mkdir(parents=True, exist_ok=True)
        self.dist.mkdir(parents=True, exist_ok=True)
        subprocess.check_output([self.npm_exe, 'install'], cwd=self.npm_build_root, env=self.env)
        subprocess.check_output([self.npx_exe, 'smui-theme', 'template', 'theme'], cwd=self.npm_build_root, env=self.env)
        subprocess.check_output([self.npm_exe, 'run', 'prepare-theme'], cwd=self.npm_build_root, env=self.env)

    def build(self):
        self.prebuild()
        subprocess.check_output([self.npm_exe, 'run', 'build'], cwd=self.npm_build_root, env=self.env)
        # files = list(filter(lambda x: os.path.splitext(x)[1] != '.map', os.listdir(dist)))
        out_list = list(filter(lambda x: x.is_file(), self.dist.rglob('*')))
        if self.out_file_list:
            out_list_text = "\n".join([f"{p.relative_to(self.dist).as_posix()}!{p.as_posix()}" for p in out_list])
            if not self.out_file_list.exists() or self.out_file_list.read_text() != out_list_text:
                self.out_file_list.write_text(out_list_text)
        return out_list

    def run(self):
        subprocess.check_call([self.npm_exe, 'run', 'start'], cwd=self.npm_build_root, env=self.env)

    @ staticmethod
    def CreateArgParser(argparser: argparse.ArgumentParser):
        argparser.add_argument("--generate", action="store_true", default=False, help="Npm generate")
        argparser.add_argument("--compile", action="store_true", default=False, help="Npm build")
        argparser.add_argument("--subdir", type=pathlib.Path, default=None, help="")
        argparser.add_argument("--buildroot", type=pathlib.Path, default=None, help="")
        argparser.add_argument("--out-file-list", type=pathlib.Path, default=None, help="")
        return argparser

    @ staticmethod
    def ArgHandler(args, *_extra: list[str]):
        obj = SvelteBuilder(pathlib.Path(args.subdir), pathlib.Path(args.reporoot), args.buildroot, args.out_file_list)
        if args.generate:
            obj.generate()
        if args.compile:
            obj.build()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    SvelteBuilder.CreateArgParser(parser)
    SvelteBuilder.ArgHandler(parser.parse_args())

#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
PROJECT_NAME = "papertrail"
CONTAINER_PROVIDER = "detect"


class PaperTrailBuildException(Exception):
    pass


def in_venv():
    return sys.prefix != sys.base_prefix


def container():
    provider = container.__dict__.get("provider", None)
    if provider:
        return provider

    if shutil.which("podman") is None:
        if shutil.which("dnf"):
            subprocess.check_call(["sudo", "dnf", "-y", "install", "podman"])
        elif shutil.which("apt-get"):
            subprocess.check_call(["sudo", "apt-get", "-y", "install", "podman"])
        else:
            raise PaperTrailBuildException("Cannot find or install container")
    container.__dict__["provider"] = "podman"
    return "podman"


def merge_path(fpath: Path | str | None, work_dir: Path | str | None):
    if fpath is None:
        return None
    fpath = Path(fpath)
    if fpath.is_absolute():
        return fpath
    else:
        return Path(work_dir or ".") / fpath


def container_build(image_name: str = f"{PROJECT_NAME}_build", export_file: Path | str | None = None, work_dir: Path | str | None = None):
    subprocess.check_call([container(), "build", "-t", image_name, "--rm", "-f", (SCRIPT_DIR.parent / "Dockerfile").as_posix()])
    export_file = merge_path(export_file, work_dir)
    if export_file:
        subprocess.check_call([container(), "save", "-o", export_file.as_posix(), image_name])


def container_test(image_name: str = f"{PROJECT_NAME}_build", import_file: Path | str | None = None, work_dir: Path | str | None = None):
    image_list = subprocess.check_output([container(), "image", "list"], text=True)
    if image_name not in image_list:
        import_file = merge_path(import_file, work_dir)
        if import_file is None:
            raise PaperTrailBuildException(f"Cannot find image: {image_name} and no import file specified")
        subprocess.check_call([container(), "load", "--input", import_file.as_posix()])
    import container_test

    container_test.container_test(work_dir=work_dir, image_name=image_name)


def find_venv_python(venv_dir: Path):
    venv_python = shutil.which("python3", path=venv_dir / "bin") or shutil.which("python3", path=venv_dir / "Scripts")
    if not venv_python:
        raise PaperTrailBuildException("Cannot find venv python")
    return venv_python


def build(work_dir: Path | str | None = None):
    work_dir = Path(work_dir or ".").absolute()
    src_dir = SCRIPT_DIR.parent.absolute()
    work_dir.mkdir(exist_ok=True, parents=True)
    if not in_venv():
        buildvenv_dir = work_dir / "build_venv"
        appvenv_dir = work_dir / "venv"
        subprocess.check_call([sys.executable, "-m", "venv", buildvenv_dir.as_posix()])
        subprocess.check_call([sys.executable, "-m", "venv", appvenv_dir.as_posix()])

        subprocess.check_call(
            [(appvenv_dir / "bin" / "pip").as_posix(), "install", "--upgrade", "pip", "-r", (src_dir / "requirements.txt").as_posix()]
        )

        subprocess.check_call([find_venv_python(buildvenv_dir), __file__, *sys.argv[1:]])
        return

    subprocess.check_call([sys.executable, "-m", "pip", "install", "--upgrade", "pip", "-r", (src_dir / "requirements.txt").as_posix()])
    if (src_dir / "buildverse").exists():
        subprocess.check_call([sys.executable, "-m", "pip", "install", "--force-reinstall", (src_dir / "buildverse").as_posix()])
    else:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "buildverse==0.0.7"])
    build_svelte(work_dir)
    warm_doctr_cache(work_dir)


def warm_doctr_cache(work_dir: Path):
    src_dir = SCRIPT_DIR.parent.absolute()
    appvenv_dir = work_dir / "venv"
    subprocess.check_call(
        [find_venv_python(appvenv_dir), (src_dir / "papertrail.py").as_posix(), f"--warm-up-doctr-cache={work_dir.as_posix()}"]
    )


def build_svelte(work_dir: Path):
    import buildverse.externaltools
    import buildverse.svelte

    buildverse.externaltools.GetBinary("typesense-server", binpath=work_dir)

    svelte_dir = work_dir / "svelte"
    (work_dir / "bin").mkdir(exist_ok=True)
    os.environ["DEVEL_BUILDPATH"] = svelte_dir.as_posix()
    os.environ["DEVEL_BINPATH"] = (work_dir / "bin").as_posix()
    os.environ["NPM_BUILD_ROOT"] = (work_dir / "npm").as_posix()
    src_dir = SCRIPT_DIR.parent.absolute()

    builder = buildverse.svelte.SvelteBuilder(reporoot=src_dir, subdir=src_dir / "web", buildroot=svelte_dir, out_file_list=None)
    builder.generate()
    builder.build()
    print(src_dir)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-dir", type=Path, default=Path(), help="Working Directory")
    subparsers = parser.add_subparsers(help="sub-command help")

    container_build_parser = subparsers.add_parser("container-build", help="container Build")
    container_test_parser = subparsers.add_parser("container-test", help="container Test")

    build_parser = subparsers.add_parser("build", help="Build")
    test_parser = subparsers.add_parser("test", help="Test")

    container_build_parser.add_argument("--export-file", type=Path, help="Image file to export")
    container_build_parser.add_argument("--image-name", type=str, help="Image name to export")

    container_test_parser.add_argument("--import-file", type=Path, help="Image file to import")
    container_test_parser.add_argument("--image-name", type=str, help="Image name to export")

    container_build_parser.set_defaults(func=container_build)
    container_test_parser.set_defaults(func=container_test)
    build_parser.set_defaults(func=build)
    args = parser.parse_args()
    func = args.func
    argdict = {k: v for k, v in args.__dict__.items() if v}
    argdict.pop("func")
    func(**argdict)

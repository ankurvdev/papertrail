#!/usr/bin/env python3
import sys
import argparse
import subprocess
from pathlib import Path
import platform
import urllib.request
import tarfile
import build.svelte
import os

src_dir = Path(__file__).parent
parser = argparse.ArgumentParser()
parser.add_argument('--work-dir', type=Path, default=src_dir / 'work')
args = parser.parse_args()

work_dir: Path = args.work_dir
work_dir.mkdir(exist_ok=True, parents=True)

venv_dir = work_dir / 'venv'
if not venv_dir.exists():
    subprocess.check_call([sys.executable, "-m", "venv", venv_dir.as_posix()])
    subprocess.check_call([(venv_dir / "bin" / "pip").as_posix(), "install", "-r", (src_dir / "requirements.txt").as_posix()])

typesense_bin = work_dir / 'typesense-server'
if not typesense_bin.exists():
    archive = work_dir / 'typesense-server.tar.gz'
    if not archive.exists():
        x64url = 'https://dl.typesense.org/releases/0.24.1/typesense-server-0.24.1-linux-amd64.tar.gz'
        a64url = 'https://dl.typesense.org/releases/0.24.1/typesense-server-0.24.1-linux-arm64.tar.gz'
        url = x64url if platform.machine() == 'AMD64' else a64url
        urllib.request.urlretrieve(url, archive.as_posix())
    tar = tarfile.open(archive.as_posix())
    tar.extractall(path=work_dir)
    tar.close()

svelte_dir = work_dir / 'svelte'
(work_dir / 'bin').mkdir(exist_ok=True)
os.environ['DEVEL_BUILDPATH'] = svelte_dir.as_posix()
os.environ['DEVEL_BINPATH'] = (work_dir / 'bin').as_posix()
os.environ['NPM_BUILD_ROOT'] = (work_dir /'npm').as_posix()
builder = build.svelte.SvelteBuilder(reporoot=src_dir, subdir=src_dir / 'web', buildroot=svelte_dir, out_file_list=None)
builder.generate()
builder.build()
print(src_dir)

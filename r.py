#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys
from pathlib import Path

import buildverse.externaltools
import buildverse.svelte

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

typesense_bin = buildverse.externaltools.GetBinary('typesense-server', binpath=work_dir)

svelte_dir = work_dir / 'svelte'
(work_dir / 'bin').mkdir(exist_ok=True)
os.environ['DEVEL_BUILDPATH'] = svelte_dir.as_posix()
os.environ['DEVEL_BINPATH'] = (work_dir / 'bin').as_posix()
os.environ['NPM_BUILD_ROOT'] = (work_dir /'npm').as_posix()
builder = buildverse.svelte.SvelteBuilder(reporoot=src_dir, subdir=src_dir / 'web', buildroot=svelte_dir, out_file_list=None)
builder.generate()
builder.build()
print(src_dir)

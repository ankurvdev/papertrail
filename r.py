#!/usr/bin/env python3
import sys
import argparse
import subprocess
from pathlib import Path
src_dir = Path(__file__).parent
parser = argparse.ArgumentParser()
parser.add_argument('--work-dir', type=Path, default=src_dir / 'build')
args = parser.parse_args()

work_dir: Path = args.work_dir
work_dir.mkdir(exist_ok=True, parents=True)

venv_dir = work_dir / 'venv'
if not venv_dir.exists():
    subprocess.check_call(sys.executable, "-m", "venv", venv_dir.as_posix())
    subprocess.check_call((venv_dir / "bin" / "pip").as_posix(), "install", "-r", (src_dir / "requirements.txt").as_posix())

print(src_dir)

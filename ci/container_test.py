import datetime
import shlex
import shutil
import subprocess
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

from run import container


class PaperTrailTestException(Exception):
    pass


def clean_cache_dir(work_dir: Path):
    cache_dir = work_dir / "cache"
    if cache_dir.exists():
        shutil.rmtree(cache_dir, ignore_errors=True)
    cache_dir.mkdir(exist_ok=True, parents=True)
    return cache_dir


def download_dataset_zip(work_dir: Path, name: str, url: str, sub_dir: Path):
    zip_fpath = work_dir / f"{name}.zip"
    extract_dir = work_dir / f"{name}_dataset"
    extract_dir.mkdir(exist_ok=True, parents=True)
    dataset_dir = extract_dir / sub_dir
    if dataset_dir.exists():
        return dataset_dir

    urllib.request.urlretrieve(url=url, filename=zip_fpath.as_posix())
    with zipfile.ZipFile(zip_fpath) as zf:
        zf.extractall(extract_dir)
    if not dataset_dir.exists():
        raise PaperTrailTestException(f"Cannot find f{dataset_dir.as_posix()}")
    return dataset_dir


def download_docback_dataset(work_dir: Path):
    return download_dataset_zip(
        work_dir,
        "docbank",
        "https://github.com/doc-analysis/DocBank/archive/refs/heads/master.zip",
        Path("DocBank-master/DocBank_samples/DocBank_samples"),
    )


def download_funds_dataset(work_dir: Path):
    return download_dataset_zip(
        work_dir, "funsd", "https://guillaumejaume.github.io/FUNSD/dataset.zip", Path("dataset/testing_data/images")
    )


def download_ocrmypdf_tests_dataset(work_dir: Path):
    return download_dataset_zip(
        work_dir,
        "ocrmypdf",
        "https://github.com/ocrmypdf/OCRmyPDF/archive/refs/heads/main.zip",
        Path("OCRmyPDF-main/tests/resources"),
    )


def test_scanning(work_dir: Path, data_volumes: dict[Path, Path]):
    cache_dir = work_dir / "cache"
    time.sleep(5)
    if not (cache_dir / "data.sqlite").exists():
        raise
    pending = set(
        [
            mnt_point / fpath.relative_to(src_path)
            for mnt_point, src_path in data_volumes.items()
            for fpath in src_path.rglob("*")
            if fpath.is_file()
        ]
    )
    lastprogress = datetime.datetime.now()
    while pending:
        time.sleep(1)
        now = datetime.datetime.now()
        cache_files = set([fpath for fpath in cache_dir.rglob("*")])
        symlinks = set([fpath.readlink() for fpath in cache_files if fpath.is_symlink()])
        done = pending.intersection(symlinks)
        for fpath in sorted(done):
            print(f"{fpath.as_posix()}")
        if not done and (now - lastprogress).total_seconds() > 60:
            pendingfiles = "\n\t".join([fpath.as_posix() for fpath in pending])
            completed = "\n\t".join([fpath.as_posix() for fpath in symlinks])
            raise PaperTrailTestException(
                f"No files processed in 30s. Seems hanged.\nPending ...\n\t{pendingfiles}\nCompleted\n\t{completed}"
            )
        else:
            lastprogress = now
            pending = pending - done


def container_test(work_dir: Path | str | None = None, image_name: str = "papertrail_build", container_name: str = "test"):
    work_dir = Path(work_dir or ".").absolute()
    rovolumes: dict[Path, Path] = {
        # Path("/data/funds"): download_funds_dataset(work_dir),
        # Path("/data/docbank"): download_docback_dataset(work_dir),
        Path("/data/ocrmypdf"): download_ocrmypdf_tests_dataset(work_dir),
    }
    rwvolumes: dict[Path, Path] = {Path("/cache"): clean_cache_dir(work_dir)}
    cmd = (
        [container(), "run", "-d", "--rm", "--name", container_name]
        + [cmdarg for mnt, src in rovolumes.items() for cmdarg in ["-v", f"{src.absolute().as_posix()}:{mnt.as_posix()}:ro,Z"]]
        + [cmdarg for mnt, src in rwvolumes.items() for cmdarg in ["-v", f"{src.absolute().as_posix()}:{mnt.as_posix()}:Z"]]
        + [f"localhost/{image_name}"]
    )
    sys.stderr.write(f"Executing...\n{shlex.join(cmd)}\n")
    container_id = subprocess.check_output(
        cmd,
        text=True,
    ).strip()

    def tear_down():
        sys.stdout.write(subprocess.check_output([container(), "logs", container_id], text=True))
        subprocess.run([container(), "stop", container_id], check=False)

    try:
        test_scanning(work_dir, rovolumes)
        # test_typesense_response(port)
    except Exception as exc:
        tear_down()
        sys.stderr.write(f"Failed tests: {exc}")
        raise

    tear_down()


if __name__ == "__main__":
    container_test()

import argparse
import asyncio
import hashlib
import json
import os
import shutil
import socket
import sqlite3
import subprocess
import sys
import threading
import time
from pathlib import Path

import aiohttp
import aiohttp.web
import doctr.io
import doctr.models
import doctr.models.predictor.pytorch
import pypdfium2
import typesense
import typesense.client
import typesense.exceptions


class TypesenseBridgeException(Exception):
    pass


class OCRException(Exception):
    pass


class PaperTrailService:
    def __init__(self, work_dir: Path | str | None, port: int = 5000):
        self.curr_dir = Path(__file__).absolute().parent
        sys.path.insert(1, self.curr_dir.parent.as_posix())
        work_dir = Path(work_dir or (self.curr_dir / "work"))
        self.port = port
        self.work_dir = work_dir
        self.client: typesense.Client | None = None
        self.typesense_server: subprocess.Popen[bytes] | None = None
        self.model: doctr.models.predictor.pytorch.OCRPredictor | None = None
        self.server_thread: threading.Thread | None = None
        self.tasks: dict[str, threading.Thread] = {}
        self._stop_requested: threading.Event = threading.Event()
        self.mutex = threading.Lock()

        self.work_dir.mkdir(parents=True, exist_ok=True)
        self.db_file = work_dir / "data.sqlite"
        conn = self._get_sqlite_conn()
        c = conn.cursor()
        c.execute("CREATE TABLE IF NOT EXISTS fileinfo (path text PRIMARY KEY, lastmodified INTEGER, size INTEGER, md5hash char(32))")
        conn.commit()

        typesense_binary = shutil.which("typesense-server", path=self.curr_dir) or shutil.which("typesense-server", path=work_dir)
        self.typesense_binary = Path(typesense_binary or "typesense-server")
        if not self.typesense_binary.exists():
            raise TypesenseBridgeException("Cannot find typesense-server")
        if "DOCTR_CACHE_DIR" not in os.environ:
            doctr_cache = work_dir / "doctr-cache"
            if not doctr_cache.exists() and (self.curr_dir / "doctr-cache").exists():
                doctr_cache = self.curr_dir / "doctr-cache"
            if not doctr_cache.exists():
                doctr_cache.mkdir()
            os.environ["DOCTR_CACHE_DIR"] = doctr_cache.absolute().as_posix()

        self.typesense_work_dir = self.work_dir / "typesense"
        self.typesense_work_dir.mkdir(exist_ok=True)

    def warm_up_doctr_cache(self):
        self.model = doctr.models.ocr_predictor(pretrained=True)

    def start_analyze_all(self):
        with self.mutex:
            if "analyze" in self.tasks:
                return
            thrd = threading.Thread(target=lambda that: that.analyze_all(), args=[self])
            self.tasks["analyze"] = thrd
        thrd.start()

    def start_typesense(self):
        shutil.rmtree(self.typesense_work_dir.as_posix())
        self.typesense_work_dir.mkdir(exist_ok=True)
        while not self._stop_requested.is_set():
            sys.stdout.write("Starting Typesense Service ... \n")
            self.typesense_server = subprocess.check_call(
                [
                    self.typesense_binary.as_posix(),
                    f"--data-dir={self.typesense_work_dir.as_posix()}",
                    "--api-key=test",
                    f"--log-dir={self.typesense_work_dir.as_posix()}",
                ]
            )

    def start(self):
        sys.stdout.write("Starting Typesense Service ... \n")
        self.server_thread = threading.Thread(target=lambda that: that.start_typesense(), args=[self])
        wait_time_in_seconds = 10
        granularity = 100
        port = 8108
        for i in range(granularity):
            time.sleep(wait_time_in_seconds / granularity)
            soc = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            try:
                soc.connect(("localhost", port))
                soc.shutdown(2)
                break
            except ConnectionRefusedError as exc:
                if i == granularity:
                    raise TypesenseBridgeException("Cannot start typesense-server") from exc

        self.client = typesense.Client(
            {"api_key": "test", "nodes": [{"host": "localhost", "port": f"{port}", "protocol": "http"}], "connection_timeout_seconds": 2}
        )

        # Drop pre-existing collection if any
        try:
            self.client.collections["documents"].delete()
        except typesense.exceptions.TypesenseClientError:
            pass

        self.client.collections.create(
            {
                "name": "documents",
                "fields": [
                    {"name": "filename", "type": "string", "infix": True},
                    {"name": "url", "type": "string"},
                    {"name": "tags", "type": "string[]", "facet": True},
                    {"name": "created_at", "type": "int32"},
                    {"name": "contents", "type": "string"},
                ],
                "default_sorting_field": "created_at",
            }
        )

        sys.stdout.write("Initializing OCR Model... \n")
        self.model = doctr.models.ocr_predictor(pretrained=True)
        sys.stdout.write("Initializing Web Service... \n")
        app = aiohttp.web.Application()
        app.add_routes([aiohttp.web.get(r"/app/{filepath:.*}", self.websvc_app)])
        app.add_routes([aiohttp.web.get("/", self.websvc_main)])
        app.add_routes([aiohttp.web.get(r"/files/{filepath:.*}", self.websvc_files)])
        app.add_routes([aiohttp.web.get(r"/scan/{filepath:.*}", self.websvc_scan)])
        app.add_routes([aiohttp.web.get("/search", self.websvc_search)])
        self.start_analyze_all()
        # logging.basicConfig(level=logging.DEBUG, filename=str(self.rundir / f"opendirdiff_log_{os.getpid()}.log"))
        aiohttp.web.run_app(app, port=self.port)

    async def websvc_app(self, request: aiohttp.web.Request) -> aiohttp.web.FileResponse:
        filepath = request.match_info.get("filepath", "")
        if len(filepath) == 0:
            filepath = "index.html"
        abspath = self._detect_path("svelte/dist") / Path(filepath)
        return aiohttp.web.FileResponse(abspath.as_posix())

    async def websvc_main(self, _request: aiohttp.web.Request) -> aiohttp.web.FileResponse:
        raise aiohttp.web.HTTPFound("/app/")

    async def websvc_files(self, request: aiohttp.web.Request) -> aiohttp.web.FileResponse:
        abspath = Path("/") / Path(request.match_info["filepath"])
        if not self.verify(abspath):
            raise TypesenseBridgeException(f"Cannot find {abspath.as_posix()}")
        return aiohttp.web.FileResponse(abspath.as_posix())

    async def websvc_scan(self, request: aiohttp.web.Request) -> aiohttp.web.Response:
        abspath = Path("/") / Path(request.match_info["filepath"])
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, lambda: self.scan(abspath))
        self.start_analyze_all()
        return aiohttp.web.Response(text=abspath.as_posix())

    async def websvc_search(self, request: aiohttp.web.Request) -> aiohttp.web.Response:
        if not self.client:
            raise TypesenseBridgeException("typesense client unavailable")
        loop = asyncio.get_running_loop()
        search_query = dict(request.query)
        search_query.setdefault("query_by", "filename,contents")
        search_query.setdefault("infix", "always,off")
        if "q" not in search_query:
            raise TypesenseBridgeException("empty query")
        result = await loop.run_in_executor(None, lambda: self.client.collections["documents"].documents.search(search_query))
        return aiohttp.web.json_response(result)

    def _detect_path(self, relpath: Path):
        for base in (self.curr_dir, self.work_dir):
            fullpath = base / relpath
            if fullpath.exists():
                return fullpath.absolute()
        raise TypesenseBridgeException(f"Cannot find {relpath.as_posix()}")

    def __del__(self):
        self.client = None
        self._stop_requested.set()
        if self.typesense_server:
            self.typesense_server.terminate()
            self.typesense_server.wait()

    def wait_for_stop(self):
        pass

    def scan(self, scan_dir: Path):
        files: Path = []
        for fpath in scan_dir.rglob("*"):
            if fpath.is_file():
                files.append(fpath.absolute())
        self._verify_or_add_entry(files)

    def get_all_files(self) -> dict[Path, str]:
        conn = self._get_sqlite_conn()
        c = conn.cursor()
        c.execute("select path,md5hash from fileinfo")
        return {Path(row[0]): row[1] for row in c.fetchall()}

    def _verify_or_add_entry(self, files: list[Path]):
        known_files = self.get_all_files()
        for fpath in files:
            if fpath in known_files:
                continue
            sys.stdout.write(f"Adding file: {fpath.as_posix()}\n")
            fstat = fpath.stat()
            mtime = fstat.st_mtime
            size = fstat.st_size
            conn = self._get_sqlite_conn()
            c = conn.cursor()
            c.execute("insert into fileinfo values(?,?,?,?)", (fpath.as_posix(), mtime, size, ""))
            conn.commit()

    def verify(self, fpath: Path):
        conn = self._get_sqlite_conn()
        c = conn.cursor()
        if not fpath.exists():
            c.execute("delete from fileinfo where path = ?", (fpath.as_posix(),))
            return True
        fstat = fpath.stat()
        mtime = fstat.st_mtime
        size = fstat.st_size
        update = False
        found = False
        for row in c.execute("select * from fileinfo WHERE path=?", (fpath.as_posix(),)):
            found = True
            update = row[1] != mtime or row[2] != size
        if update:
            c.execute("update fileinfo set lastmodified = ?, size = ?, md5hash = ? where path = ? ", (mtime, size, "", fpath.as_posix()))
        return found

    def _get_sqlite_conn(self):
        for i in range(10):
            try:
                return sqlite3.connect(self.db_file.as_posix(), detect_types=sqlite3.PARSE_DECLTYPES)
            except sqlite3.Error:
                sys.stderr.write(f"Cannot open sqlite. Attempt {i}. Retrying in 5s")
                time.sleep(5)
        raise TypesenseBridgeException("Cannot open sqlite")

    # Dir
    #   file0
    #   page0_ocr_doctr.json
    #   page1_thumbnail.jpg
    #   page2_pdf2text.json
    #   page3_ocr_tesseract.json
    #   page4_tags.json
    #   tags.json
    def analyze_file(self, fpath: Path, md5sum: str, status: str) -> bool:
        sys.stderr.write(f"{status} Analyzing {fpath.as_posix()}\n")
        actions: dict[str, str] = {}
        changed = False
        if len(md5sum) == 0:
            hash_md5 = hashlib.md5()
            with fpath.open("rb") as f:
                for chunk in iter(lambda: f.read(4096), b""):
                    hash_md5.update(chunk)
            md5sum = hash_md5.hexdigest()
            fstat = fpath.stat()
            mtime = fstat.st_mtime
            size = fstat.st_size

            conn = self._get_sqlite_conn()
            c = conn.cursor()
            c.execute("update fileinfo set lastmodified = ?, size = ?, md5hash = ? where path = ?", (mtime, size, md5sum, fpath.as_posix()))
            c.close()
            conn.commit()
            actions["md5sum"] = md5sum
            changed = True

        metadata_dir = self.work_dir / md5sum
        metadata_dir.mkdir(parents=True, exist_ok=True)
        symlink = metadata_dir / "file"
        if not symlink.exists():
            symlink.unlink(missing_ok=True)
            os.symlink(fpath, symlink.as_posix())
            changed = True

        if len(list(metadata_dir.glob("*.textdata.json"))) == 0:
            try:
                doc = None
                if fpath.suffix.lower() in (".jpg", ".png"):
                    doc = doctr.io.DocumentFile.from_images(fpath.as_posix())
                    (metadata_dir / "ocr.textdata.json").write_text(json.dumps(self.model(doc).export()))
                    changed = True

                if fpath.suffix.lower() in (".pdf"):
                    doc = doctr.io.DocumentFile.from_pdf(fpath.as_posix())
                    (metadata_dir / "ocr.textdata.json").write_text(json.dumps(self.model(doc).export()))
                    changed = True

                if fpath.suffix.lower() in (".pdf"):
                    changed = True
                    pages = []
                    for page in pypdfium2.PdfDocument(fpath.as_posix()):
                        textpage = page.get_textpage()
                        lines: list = []

                        for rect_index in range(textpage.count_rects()):
                            rect = textpage.get_rect(rect_index)
                            text = textpage.get_text_bounded(*rect)
                            if len(text) == 0:
                                continue
                            lines.append({"words": [{"value": text, "geometry": rect}]})
                        if len(lines) > 0:
                            pages.append({"blocks": [{"lines": lines}]})

                    if False:
                        reader = PyPDF2.PdfReader(fpath.as_posix())
                        lines = []
                        pages = []

                        def visitor_text(text, cm, tm, fontDict, fontSize):
                            if len(text) == 0:
                                return
                            lines.append({"words": [{"value": text, "geometry": []}]})
                            print(f"{tm}:{text}")

                        for page in reader.pages:
                            page.extract_text(visitor_text=visitor_text)
                            pages.append({"blocks": [{"lines": lines}]})
                    jsondata = {"pages": pages}
                    (metadata_dir / "pdftext.textdata.json").write_text(json.dumps(jsondata))
            except (pypdfium2.PdfiumError, OSError) as exc:
                sys.stderr.write(f"Error Processing PDF: {str(exc)}\n")
                pass

        contents = ""
        for text_data in metadata_dir.glob("*.textdata.json"):
            json_doc = json.loads(text_data.read_text())
            blocks: list[str] = []
            for page in json_doc["pages"]:
                for block in page["blocks"]:
                    lines: list[str] = []
                    for line in block["lines"]:
                        words: list[str] = []
                        for word in line["words"]:
                            words.append(word["value"])
                        lines.append(" ".join(words))
                    blocks.append("\n".join(lines))
            contents = "\n\n".join(blocks)

        typesense_dict: dict[str, object] = {
            "filename": fpath.name,
            "url": (Path("/files") / fpath.relative_to(Path("/"))).as_posix(),
            "tags": [],
            "created_at": 0,
            "contents": contents,
        }
        if self.client:
            self.client.collections["documents"].documents.create(typesense_dict)

        return changed

    def analyze_all(self):
        keep_going = True
        while keep_going:
            keep_going = False
            known_files = self.get_all_files()
            count = 0
            total = len(known_files)
            for fpath, md5sum in known_files.items():
                count += 1
                try:
                    keep_going = self.analyze_file(Path(fpath), md5sum, ("[" + str(count) + "/" + str(total) + "]")) or keep_going
                except OSError as exc:
                    sys.stderr.write(f"Cannot Analyze {str(fpath)}: {str(exc)}")

        with self.mutex:
            del self.tasks["analyze"]


parser = argparse.ArgumentParser(description="Scan Directories for duplicates")
parser.add_argument("--work-dir", type=Path, default=None)
parser.add_argument("--port", type=int, default=5000)
parser.add_argument("--warm-up-doctr-cache", type=Path, default=None, help="Warm up the doctr cache")
parser.add_argument("--analyze-file", type=Path, default=None, help="Analyze File")
parser.add_argument("dirs", type=Path, nargs="*")
args = parser.parse_args()
if args.analyze_file is not None:
    svc = PaperTrailService(work_dir=args.warm_up_doctr_cache, port=args.port)
    svc.analyze_file(args.analyze_file, "", "")
    sys.exit(0)

if args.warm_up_doctr_cache is not None:
    svc = PaperTrailService(work_dir=args.warm_up_doctr_cache, port=args.port)
    svc.warm_up_doctr_cache()
    sys.exit(0)

svc = PaperTrailService(work_dir=args.work_dir, port=args.port)
for sdir in args.dirs:
    svc.scan(Path(sdir))
svc.start()
svc.wait_for_stop()

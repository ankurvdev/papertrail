import argparse
import hashlib
import json
import os
import sqlite3
import sys
from pathlib import Path

import typesense
import typesense.client
import typesense.exceptions
from doctr.io import DocumentFile
from doctr.models import ocr_predictor

curr_dir = Path(__file__).absolute().parent
sys.path.insert(1, curr_dir.parent.as_posix())


class TypesenseBridgeException(Exception):
    pass


client = typesense.Client({
    'api_key': 'test',
    'nodes': [{
        'host': 'localhost',
        'port': '8108',
        'protocol': 'http'
    }],
    'connection_timeout_seconds': 2
})

# Drop pre-existing collection if any
try:
    client.collections['documents'].delete()
except typesense.exceptions.TypesenseClientError:
    pass

# Create a collection

create_response = client.collections.create({
    "name": "documents",
    "fields": [
        {"name": "filename", "type": "string"},
        {"name": "url", "type": "string"},
        {"name": "tags", "type": "string[]", "facet": True},
        {"name": "created_at", "type": "int32", "facet": True},
        {"name": "contents", "type": "string"}
    ],
    "default_sorting_field": "created_at"
})

model = ocr_predictor(pretrained=True)


class OCRException(Exception):
    pass


class ScanForDuplicates:
    def __init__(self, work_dir: Path):
        self.work_dir = work_dir
        self.work_dir.mkdir(parents=True, exist_ok=True)
        self.db_file = work_dir / "data.sqlite"
        self.conn = sqlite3.connect(self.db_file.as_posix(), detect_types=sqlite3.PARSE_DECLTYPES)
        c = self.conn.cursor()
        c.execute('CREATE TABLE IF NOT EXISTS fileinfo (path text PRIMARY KEY, lastmodified INTEGER, size INTEGER, md5hash char(32))')

    def __del__(self):
        self.conn.commit()

    def scan(self, scan_dir: Path):
        for fpath in scan_dir.rglob('*'):
            if fpath.is_file():
                self._verify_or_add_entry(fpath.absolute())

    def get_all_files(self):
        c = self.conn.cursor()
        c.execute('select * from fileinfo')
        return c.fetchall()

    def _verify_or_add_entry(self, fpath: Path):
        if self.verify(fpath):
            return
        fstat = fpath.stat()
        mtime = fstat.st_mtime
        size = fstat.st_size
        c = self.conn.cursor()
        c.execute('insert into fileinfo values(?,?,?,?)', (fpath.as_posix(), mtime, size, ""))

    def verify(self, fpath: Path):
        c = self.conn.cursor()
        if not fpath.exists():
            c.execute('delete from fileinfo where path = ?', (fpath.as_posix(),))
            return True
        fstat = fpath.stat()
        mtime = fstat.st_mtime
        size = fstat.st_size
        update = False
        found = False
        for row in c.execute('select * from fileinfo WHERE path=?', (fpath.as_posix(),)):
            found = True
            update = row[1] != mtime or row[2] != size
        if update:
            c.execute('update fileinfo set lastmodified = ?, size = ?, md5hash = ? where path = ? ', (mtime, size, "", fpath.as_posix()))
        return found

    def _analyze_file(self, fpath: Path, md5sum: str, status):
        sys.stderr.write(f"{status} Analyzing {fpath.as_posix()}\n")
        if len(md5sum) == 0:
            hash_md5 = hashlib.md5()
            with fpath.open("rb") as f:
                for chunk in iter(lambda: f.read(4096), b""):
                    hash_md5.update(chunk)
            md5sum = hash_md5.hexdigest()
            fstat = fpath.stat()
            mtime = fstat.st_mtime
            size = fstat.st_size

            c = self.conn.cursor()
            c.execute('update fileinfo set lastmodified = ?, size = ?, md5hash = ? where path = ?', (mtime, size, md5sum, fpath.as_posix()))
            c.close()
            self.conn.commit()

        metadata_dir = self.work_dir / md5sum
        metadata_dir.mkdir(parents=True, exist_ok=True)
        symlink = metadata_dir / "file"
        if not symlink.exists():
            os.symlink(fpath, symlink.as_posix())

        ocr_data = metadata_dir / "ocr.json"
        if not ocr_data.exists():
            doc = None
            if fpath.suffix == '.pdf':
                pass
                # doc = DocumentFile.from_pdf(fpath.as_posix())
            elif fpath.suffix.lower() in ('.jpg', '.png'):
                doc = DocumentFile.from_images(fpath.as_posix())
            else:
                sys.stderr.write(f"Unrecognized file extension: {fpath.as_posix()}\n")
            # Analyze
            if doc:
                ocr_data.write_text(json.dumps(model(doc).export()))
        if ocr_data.exists():
            json_doc = json.loads(ocr_data.read_text())
            words = []
            for page in json_doc["pages"]:
                for block in page["blocks"]:
                    for line in block["lines"]:
                        for word in line["words"]:
                            words.append(word["value"])
            contents = " ".join(words)
            typesense_dict = {
                'filename': fpath.name,
                'url': fpath.as_posix(),
                'tags': [],
                'created_at': 0,
                'contents': contents
            }
            client.collections['documents'].documents.create(typesense_dict)

        return md5sum

    def analyze_all(self):
        c = self.conn.cursor()
        files: dict[str, str] = {}
        for row in c.execute('select * from fileinfo'):
            files[row[0]] = row[3]
        count = 0
        total = len(files)
        for fpath, md5sum in files.items():
            count += 1
            self._analyze_file(Path(fpath), md5sum, ("[" + str(count) + "/" + str(total) + "]"))


parser = argparse.ArgumentParser(description='Scan Directories for duplicates')
parser.add_argument('--work-dir', type=Path)
parser.add_argument('dirs', type=Path, nargs='*')
args = parser.parse_args()
scanner = ScanForDuplicates(args.work_dir)
for sdir in args.dirs:
    scanner.scan(Path(sdir))
scanner.analyze_all()

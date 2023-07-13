#!/usr/bin/python
import argparse
import hashlib
import os


class FindAndReplaceWithLinks:
    def __init__(self):
        self.srcfiles: dict[str, set()] = {}
        self.destfiles: dict[str, set()] = {}
        self.md5sums: dict[str, str] = {}

    def scan_src(self, src):
        for path, dirs, files in os.walk(src):
            for f in files:
                fpath = os.path.join(path, f)
                if not os.path.isfile(fpath): continue
                bname = os.path.basename(f)
                self.srcfiles.setdefault(bname, set())
                self.srcfiles[os.path.basename(f)].add(os.path.abspath(fpath))

    def scan_dst(self, dst):
        for path, dirs, files in os.walk(dst):
            for f in files:
                fpath = os.path.join(path, f)
                if not os.path.isfile(fpath): 
                    continue
                bname = os.path.basename(f)
                self.destfiles.setdefault(bname, set())
                self.destfiles[os.path.basename(f)].add(os.path.abspath(fpath))

    def _fsize(self, fpath):
        fpath = os.path.abspath(fpath)
        return os.stat(fpath).st_size

    def _generate_hash(self, fpath):
        fpath = os.path.abspath(fpath)
        if fpath in self.md5sums:
            return self.md5sums[fpath]

        hash_md5 = hashlib.md5()
        with open(fpath, "rb") as f:
            for chunk in iter(lambda: f.read(4096), b""):
                hash_md5.update(chunk)
        sum = hash_md5.hexdigest()
        self.md5sums[fpath] = sum
        return sum

    def run(self, dryrun=False):
        def _same(f1, f2):
            return self._fsize(f1) == self._fsize(f2) and self._generate_hash(f1) == self._generate_hash(f2)
        print(self.destfiles)
        for fname, dset in self.destfiles.items():
            if not fname in self.srcfiles: continue
            sset = self.srcfiles[fname]
            for d in dset:
                for s in sset:
                    if _same(d, s):
                        if dryrun:
                            print(f"ln -sf {s} {d}")
                        else:
                            os.unlink(d)
                            os.symlink(s, d)

    def main(self):
        import argparse
        parser = argparse.ArgumentParser(description='Find and Replace duplicate files with links')
        parser.add_argument('--src', type=str, nargs='*')
        parser.add_argument('--dst', type=str, nargs='*')
        parser.add_argument('--dryrun', action="store_true")

        args = parser.parse_args()
        for d in args.src: self.scan_src(d)
        for d in args.dst: self.scan_dst(d)
        self.run(args.dryrun)

if __name__ == "__main__":
    FindAndReplaceWithLinks().main()
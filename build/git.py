import os
import pathlib
import re
import subprocess
import sys
import time
from typing import Any, Optional

from . import externaltools


class GitError(Exception):
    pass


class Git:
    def __init__(self, environ: dict[str, str] | None = None, root: pathlib.Path = pathlib.Path()):
        self.root: pathlib.Path = root
        self.environ = environ or os.environ.copy()
        self.git = externaltools.GetGit().as_posix()
        self.shapat = re.compile(r'\b[0-9a-f]{40}\b$')

    def set_root(self, root: pathlib.Path):
        self.root = root

    def cmd(self, cmd: list[str], root: Optional[pathlib.Path] = None, check=True, quiet=False, **kargs: Any) -> str:
        root = root or self.root
        cmd = [self.git] + cmd
        if not quiet:
            sys.stderr.write(" ".join(cmd) + "\n")
        rslt = subprocess.run(cmd, cwd=root.as_posix(), env=self.environ, capture_output=True, check=False, text=True, **kargs)
        if check and rslt.returncode != 0:
            sys.stderr.write(rslt.stdout + "\n" + str(rslt.returncode) + "\n" + str(rslt) + "\n" + str(self.root))
            raise GitError("Failed : " + " ".join(cmd))
        return rslt.stdout

    def try_get_commit_hash(self, name: str) -> str | None:
        try:
            return self.commit_hash(name)
        except GitError:
            return None

    def create_branch(self, name: str, commit: str):
        self.cmd(["checkout", self.commit_hash(commit), "-b", name])

    def delete_branch(self, b: str):
        if f' {b}\n' in self.cmd(["branch"]):
            self.cmd(["branch", "-D", b])

    def commit_hash(self, commit: str):
        if len(commit) == 40 and re.match(self.shapat, commit):
            return commit
        return self.cmd(["rev-parse", "--quiet", commit, "--"], quiet=True).splitlines()[0].strip()

    def rebase_squash(self, commit: str, msg: str = "Squashed"):
        commit = self.commit_hash(commit)
        if self.cmd(["rev-list", "--count", f"{commit}..HEAD"]) == "1":
            return
        self.cmd(["reset", "--soft", commit])
        if len(self.cmd(["diff-index", "HEAD", "--"]).strip()) > 0:
            self.cmd(["commit", "-m", msg])
        else:
            sys.stderr.write(f"Nothing to commit for {commit} \n")

    def merge_branch(self, commit: str, rebase_squash: bool = False):
        while True:
            try:
                while (pathlib.Path(self.root / ".git" / "MERGE_HEAD").exists()):
                    sys.stderr.write("Merge maybe in progress. Retry again in 5s...\n")
                    time.sleep(5)
                self.cmd(["merge", self.commit_hash(commit)])
                break
            except GitError:
                time.sleep(5)
        if rebase_squash:
            self.rebase_squash(commit, "Squashed")

    def toplevel(self, root: pathlib.Path):
        return pathlib.Path(self.cmd(['rev-parse', '--show-toplevel'], root=root, quiet=True).strip())

    def current_branch(self):
        return self.cmd(["branch", "--show-current"]).strip()

    def branch_name_for_commit(self, commit):
        return self.cmd(["rev-parse", "--abbrev-ref", commit], quiet=True).strip()

    def dirty(self):
        return self.changes() != ""

    def changes(self):
        return self.cmd(["status", "--porcelain"])

    def query_branches(self, expr: str):
        branches: list[str] = []
        for b in self.cmd(["branch", "-a"]).splitlines():
            b = b.strip()
            rematch = re.search(expr, b)
            if not rematch:
                continue
            branches.append(rematch.group(0))
        return branches

    def checkout(self, commit: str):
        self.cmd(["checkout", commit])
        return self.commit_hash('HEAD')

    def sync(self):
        self.cmd(["fetch", "--all", "--prune"], check=False)

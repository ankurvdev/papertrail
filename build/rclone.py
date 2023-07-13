import pathlib
import subprocess
from typing import Optional
import configenv
import externaltools
import urllib.parse


class RClonePackageManager:
    def __init__(self, triplet: str):
        self.triplet = triplet
        self.rcloneexe: pathlib.Path = pathlib.Path(externaltools.GetRclone())
        self.uploaddest = configenv.ConfigEnv(None).GetConfigStr("VCPKG_PKG_CACHE_REMOTE_URL")
        self.remoteurl = urllib.parse.urlparse(str(self.uploaddest))

    def _destargs(self, path: Optional[pathlib.Path] = None) -> list[str]:
        remotedir: str = str((pathlib.Path(self.remoteurl.path) / self.triplet / pathlib.Path(path.name if path else "")).as_posix())
        return [":sftp:" + remotedir, f"--sftp-host={self.remoteurl.hostname}",
                f"--sftp-user={self.remoteurl.username}", f"--sftp-port={str(self.remoteurl.port)}", "--sftp-ask-password"]

    def findlatest(self, name: str):
        raise Exception("Cannot find latest")

    def exists(self, file: str):
        a = (subprocess.check_output([str(self.rcloneexe), "lsd"] + self._destargs())).decode()
        if len(a) == 0:
            return None
        dirs = a.splitlines()
        for d in dirs:
            print(d)
            raise Exception("TODO")

    def download(self, filename: pathlib.Path, destdir: pathlib.Path):
        try:
            cmd = [str(self.rcloneexe), "copyto"] + self._destargs(path=filename) + [str(destdir)]
            print(" ".join(cmd))
            subprocess.check_call(cmd)
            return dir
        except subprocess.CalledProcessError:
            return None

    def uploaddestinationvalid(self):
        return len(self.uploaddest) > 1

    def upload(self, filename: pathlib.Path):
        subprocess.check_call([str(self.rcloneexe), "copyto", str(filename)] + self._destargs(path=filename))

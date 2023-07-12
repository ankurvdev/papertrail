import os
import pathlib
import re
import subprocess
from typing import Any, Dict

if True:
    import rasterize


class Generator:
    def __init__(self, srcdir: pathlib.Path, outdir: pathlib.Path, mtime_ns: int = 0):
        self.bindable: dict[str, Any | str] = {}
        self.srcdir: pathlib.Path = srcdir
        self.outdir: pathlib.Path = outdir
        self.mtime_ns: int = max(mtime_ns, os.stat(__file__).st_mtime_ns)

    def LoadDictionary(self, d: Dict[str, Any]):
        def tolower(d: Dict[str, Any] | Any) -> Dict[str, Any] | Any:
            if isinstance(d, dict):
                return {k.lower(): tolower(v) for k, v in d.items()}
            return d
        self.bindable |= tolower(d)

    def GenerateFromTemplateDirectory(self, tmpld: pathlib.Path):
        generated: list[pathlib.Path] = []
        for root, _dirs, files in os.walk(tmpld):
            root = pathlib.Path(root)
            rpath = root.relative_to(tmpld)
            outrpath: pathlib.Path = self.outdir / self.ExpandTemplate(rpath.as_posix())
            outrpath.mkdir(exist_ok=True, parents=True)

            for f in files:
                srcf = root / f
                dstf = outrpath / f
                if not self._NeedsGeneration(srcf, dstf):
                    continue
                contents = srcf.read_text()
                generated.append(dstf)
                dstf.write_text(self.ExpandTemplate(contents))
        return generated

    def GenerateImagesForManifest(self, imgmanif: Dict[str, Any]):
        for _name, imgdesc in imgmanif.items():
            self.GenerateImage(imgdesc["path"], imgdesc)

    def _Run(self, cmd: list[str], env: dict[str, str] | None = None):
        self.outdir.mkdir(exist_ok=True, parents=True)
        my_env = os.environ
        if env is not None:
            for k, v in env.items():
                my_env[k] = v
        rslt = subprocess.run(cmd, cwd=self.outdir, capture_output=True, env=my_env, check=False)
        if rslt.returncode != 0:
            raise Exception(
                "Error Building : Command = " +
                " ".join(cmd) +
                "\n\n STDOUT = " +
                rslt.stdout.decode() +
                "\n\nSTDERR = " +
                rslt.stderr.decode())

    def ExpandTemplate(self, tmpl: str) -> str:
        startmarker = 'zzzs'
        endmarker = 'zzze'
        retval: str = tmpl
        offset = retval.find(startmarker, 0)
        while offset != -1:
            s = offset + len(startmarker)
            e = retval.find(endmarker, s)
            bindable = self.bindable
            val = ""
            binding = re.split("\\.|_", retval[s:e])
            for k in binding:
                if k.lower() not in bindable:
                    raise Exception(f"Cannot bind: {binding} to {self.bindable}")
                bindable: dict[str, Any | str] = bindable[k.lower()]  # type: ignore
            if not isinstance(bindable, str):
                raise Exception(f"Binding unresolved {binding}")
            val = str(bindable)
            retval = retval[0:offset] + val + retval[e + len(endmarker):]
            offset = retval.find(startmarker, offset)
        return retval

    def GenerateFileWithContents(self, path: pathlib.Path, newcontents: str):
        fname = self.outdir / path
        fname.parent.mkdir(exist_ok=True)
        contents = fname.read_text() if fname.exists() else None
        if contents == newcontents:
            return
        fname.write_text(newcontents)

    def _NeedsGeneration(self, srcf: pathlib.Path, dstf: pathlib.Path):
        return not (srcf.exists() and dstf.exists() and dstf.stat().st_mtime_ns > max(self.mtime_ns, srcf.stat().st_mtime_ns))

    def GenerateImage(self, outrelpath: pathlib.Path, imginfo: dict[str, str]):
        imgspec: dict[str, str] = self.bindable["images"]  # type: ignore
        srcsvg = self.srcdir / str(imgspec[imginfo["image"]])
        pwidth = int(imginfo["width"])
        pheight = int(imginfo["height"])
        width = int(imginfo.get("scalex", pwidth))
        height = int(imginfo.get("scaley", pheight))
        rasterize.rasterize_svg(
            srcsvg=srcsvg,
            outdir=self.outdir,
            out_rel_path=outrelpath,
            parent_width=pwidth,
            parent_height=pheight,
            width=width,
            height=height)

    def GenerateFiles(self, templates: dict[str, str]):
        for k, v in templates.items():
            self.GenerateFileWithContents(pathlib.Path(k), self.ExpandTemplate(v))

    def GenerateImages(self, images: dict[str, dict[str, str]]):
        for k, v in images.items():
            self.GenerateImage(pathlib.Path(k), v)

    def GenerateImagesFromSpec(self, imgname: str, spec: str):
        images = {}
        for lspec in spec.splitlines():
            if len(lspec.strip()) == 0:
                continue
            name, *size = lspec.split()
            imginfo = {}
            imginfo["img"] = imgname
            imginfo["width"], imginfo["height"] = size[0].split('x')
            if len(size) > 1:
                imginfo["scalex"], imginfo["scaley"] = size[1].split('x')
            images[name] = imginfo
        self.GenerateImages(images)

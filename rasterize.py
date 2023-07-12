import pathlib
import sys
import subprocess
import externaltools


def rasterize_svg(srcsvg: pathlib.Path, outdir: pathlib.Path, out_rel_path: pathlib.Path,
                  parent_width: int, parent_height: int, width: int, height: int):

    if not srcsvg.exists():
        raise Exception(f"Cannot find Image Source: {srcsvg}")
    outf = outdir / out_rel_path
    if (srcsvg.exists() and outf.exists() and outf.stat().st_mtime_ns > srcsvg.stat().st_mtime_ns):
        return
    left = int((parent_width - width) / 2)
    top = int((parent_height - height) / 2)
    zoomx = width / parent_width
    zoomy = height / parent_height
    outf.parent.mkdir(exist_ok=True, parents=True)

    try:
        from PyQt6.QtCore import QRectF, QSize, Qt
        from PyQt6.QtGui import QImage, QPainter
        from PyQt6.QtSvg import QSvgRenderer
        render = QSvgRenderer(srcsvg.as_posix())
        image = QImage(QSize(parent_width, parent_height), QImage.Format.Format_ARGB32_Premultiplied)
        pix = QPainter(image)
        image.fill(Qt.GlobalColor.transparent)
        render.render(pix, QRectF(left, top, width, height))
        pix.end()
        pix.endNativePainting()
        image.save(outf.as_posix(), format='PNG')
        return
    except Exception as ex:
        sys.stderr.write(f"PyQt6 Export Failed: {str(ex)}")

    cmd = [
        externaltools.GetRSVGConvert().as_posix(),
        "--output", outf.as_posix(),
        "--background-color", "none",
        "--x-zoom", str(zoomx),
        "--y-zoom", str(zoomy),
        "--width", str(parent_width),
        "--height", str(parent_height),
        srcsvg.as_posix()
    ]
    try:
        subprocess.check_call(cmd)
        return
    except subprocess.CalledProcessError as ex:
        sys.stderr.write(f"Command: {' '.join(cmd)}\n Failed: {str(ex)}")
        raise

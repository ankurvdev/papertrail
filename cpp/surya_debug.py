import torch


def write_tensor_ptc(tensor, file_path):
    """Write tensor to file in format that can be read by torchlib torch.load()"""
    par = torch.nn.Parameter(tensor, requires_grad=False)
    m = torch.nn.Module()
    m.register_parameter("0", par)
    tensor = torch.jit.script(m)
    tensor.save(file_path)


def read_tensor_ptc(file_path):
    m = torch.jit.load(file_path)
    return m.state_dict()["0"]


if __name__ == "__main__":
    import json
    import pathlib
    import pprint
    import sys

    import PIL.Image
    import surya
    import surya.detection
    import surya.postprocessing.heatmap

    imgfpath = sys.argv[1]
    for predsfpath in sys.argv[2:]:
        img = PIL.Image.open(imgfpath).convert("RGB")
        preds = read_tensor_ptc(predsfpath)
        bboxf = surya.postprocessing.heatmap.get_detected_boxes(preds[0].numpy(), 0.6, 0.35)
        result = surya.detection.parallel_get_lines([preds[0].numpy(), preds[1].numpy()], [3840, 2160])
        write_tensor_ptc(
            torch.tensor([poly.polygon for poly in bboxf]),
            predsfpath + ".bboxesf.pt",
        )
        write_tensor_ptc(
            torch.tensor([p.polygon for p in result.bboxes], dtype=torch.long),
            predsfpath + ".bboxesi.pt",
        )

        def polystr(poly):
            return ", ".join([f"[{p[0]}, {p[1]}]" for p in poly])

        bboxstr = "".join([f"poly = {polystr(p.polygon)}\n" for p in result.bboxes])
        pathlib.Path(predsfpath + ".bbox.txt").write_text(bboxstr, encoding="utf-8")

        result.affinity_map = None
        result.heatmap = None
        pathlib.Path(predsfpath + ".result.txt").write_text(pprint.pformat(result.__dict__), encoding="utf-8")
        bboxes = [poly.bbox for poly in result.bboxes]
        surya.postprocessing.heatmap.draw_bboxes_on_image(bboxes, img)
        img.save(predsfpath + ".bbox.overlay.png")

import functools
import pprint
import sys
from pathlib import Path
import cv2

import PIL.Image
import surya
import surya.detection
import surya.model.detection.model
import surya.postprocessing.heatmap
import torch


def write_tensor_ptc(tensor: torch.Tensor, file_path: Path) -> None:
    """Write tensor to file in format that can be read by torchlib torch.load()"""
    par = torch.nn.Parameter(tensor, requires_grad=False)
    m = torch.nn.Module()
    m.register_parameter("0", par)
    jitted = torch.jit.script(m)
    jitted.save(file_path)
    if len(tensor.shape) == 3:
        if tensor.shape[2] == 3 and tensor.dtype == torch.uint8:
            # Ensure the tensor is on the CPU and convert it to a NumPy array
            cv2.imwrite(file_path.with_suffix(".png"), tensor.cpu().numpy())
        if tensor.shape[0] == 3 and tensor.dtype == torch.float32:
            pass


def read_tensor_ptc(file_path: Path) -> torch.Tensor:
    m = torch.jit.load(file_path)
    return m.state_dict()["0"]


def polystr(poly: list[tuple[int, int]]) -> str:
    return ", ".join([f"[{p[0]}, {p[1]}]" for p in poly])


def trace_post_processing(imgfpath: Path, predsfpath: Path) -> None:
    img = PIL.Image.open(imgfpath).convert("RGB")

    preds = read_tensor_ptc(predsfpath)
    bboxf = surya.postprocessing.heatmap.get_detected_boxes(preds[0].numpy(), 0.6, 0.35)
    result = surya.detection.parallel_get_lines([preds[0].numpy(), preds[1].numpy()], [img.width, img.height])

    write_tensor_ptc(
        torch.tensor([poly.polygon for poly in bboxf]),
        predsfpath.with_suffix(".bboxesf.pt"),
    )
    write_tensor_ptc(
        torch.tensor([p.polygon for p in result.bboxes], dtype=torch.long),
        predsfpath.with_suffix(".bboxesi.pt"),
    )

    Path(predsfpath.with_suffix(".bbox.txt")).write_text(
        "".join([f"poly = {polystr(p.polygon)}\n" for p in result.bboxes]),
        encoding="utf-8",
    )

    result.affinity_map = None
    result.heatmap = None
    Path(predsfpath.with_suffix(".result.txt")).write_text(pprint.pformat(result.__dict__), encoding="utf-8")
    bboxes = [poly.bbox for poly in result.bboxes]
    surya.postprocessing.heatmap.draw_bboxes_on_image(bboxes, img)
    img.save(predsfpath.with_suffix(".bbox.overlay.png"))


def trace_image(imgfpath: Path) -> None:
    img = PIL.Image.open(imgfpath).convert("RGB")
    detector_model = surya.model.detection.model.load_model()
    processor_model = surya.model.detection.model.load_processor()

    def detector_wrapper(fn: any, *args: any, **kwargs: any) -> any:
        write_tensor_ptc(kwargs["pixel_values"], imgfpath.with_suffix(".trace.detector.pixel_values.pt"))
        logits = fn(detector_model, *args, **kwargs)
        write_tensor_ptc(logits.logits, imgfpath.with_suffix(".trace.detector.logits.pt"))
        return logits

    def processor_wrapper(fn: any, *args: any, **kwargs: any) -> any:
        if not hasattr(processor_wrapper, "counter"):
            processor_wrapper.counter = 0  # it doesn't exist yet, so initialize it
        write_tensor_ptc(torch.from_numpy(args[0]), imgfpath.with_suffix(f".trace.processor.input{processor_wrapper.counter}.pt"))
        outval = fn(processor_model, *args, **kwargs)
        write_tensor_ptc(
            torch.tensor(outval["pixel_values"][0]), imgfpath.with_suffix(f".trace.processor.output{processor_wrapper.counter}.pt")
        )
        processor_wrapper.counter += 1
        return outval

    detector_model.__class__.__call__ = functools.partial(detector_wrapper, detector_model.__class__.__call__)
    processor_model.__class__.__call__ = functools.partial(processor_wrapper, processor_model.__class__.__call__)

    output = list(surya.detection.batch_detection([img], detector_model, processor_model))
    write_tensor_ptc(torch.tensor(output[0][0][0]), imgfpath.with_suffix(".trace.preds.pt"))
    trace_post_processing(imgfpath, imgfpath.with_suffix(".trace.preds.pt"))


if __name__ == "__main__":
    trace_image(Path(sys.argv[1]))
    _a = [trace_post_processing(Path(sys.argv[1]), Path(arg)) for arg in sys.argv[2:]]

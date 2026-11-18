import functools
import os
from pathlib import Path
import shutil
import subprocess
import sys

import cv2
import numpy
import PIL.Image
import surya
import surya.detection
import surya.models

# import surya.detection.model
import surya.recognition

# import surya.recognition.model
# import surya.recognition.processor
# import surya.ocr
# import surya.postprocessing.heatmap
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
    bboxf = surya.detection.heatmap.get_detected_boxes(preds[0].numpy(), 0.6, 0.35)
    result = surya.detection.parallel_get_lines(
        [preds[0].numpy(), preds[1].numpy()], [img.width, img.height]
    )

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
    bboxes = [poly.bbox for poly in result.bboxes]
    surya.postprocessing.heatmap.draw_bboxes_on_image(bboxes, img)
    img.save(predsfpath.with_suffix(".bbox.overlay.png"))


def trace_image(imgfpath: Path) -> None:
    img = PIL.Image.open(imgfpath).convert("RGB")
    predictors = surya.model.load_predictors()
    detector_model = predictors["detector"]
    detector_processor = surya.model.detection.model.load_processor()
    recognition_model = predictors["recognition"]
    recognition_processor = surya.model.recognition.processor.load_processor()

    def detector_wrapper(fn: any, *args: any, **kwargs: any) -> any:
        write_tensor_ptc(
            kwargs["pixel_values"],
            imgfpath.with_suffix(".trace.detector.pixel_values.pt"),
        )
        logits = fn(detector_model, *args, **kwargs)
        write_tensor_ptc(
            logits.logits, imgfpath.with_suffix(".trace.detector.logits.pt")
        )
        return logits

    def processor_wrapper(fn: any, *args: any, **kwargs: any) -> any:
        if not hasattr(processor_wrapper, "counter"):
            processor_wrapper.counter = 0  # it doesn't exist yet, so initialize it
        write_tensor_ptc(
            torch.from_numpy(args[0]),
            imgfpath.with_suffix(
                f".trace.processor.input{processor_wrapper.counter}.pt"
            ),
        )
        outval = fn(detector_processor, *args, **kwargs)
        write_tensor_ptc(
            torch.tensor(outval["pixel_values"][0]),
            imgfpath.with_suffix(
                f".trace.processor.output{processor_wrapper.counter}.pt"
            ),
        )
        processor_wrapper.counter += 1
        return outval

    detector_model.__class__.__call__ = functools.partial(
        detector_wrapper, detector_model.__class__.__call__
    )
    detector_processor.__class__.__call__ = functools.partial(
        processor_wrapper, detector_processor.__class__.__call__
    )

    preds, orig_sizes = next(
        iter(surya.detection.batch_detection([img], detector_model, detector_processor))
    )
    heatmap, _affinitymap = preds[0]
    write_tensor_ptc(torch.tensor(preds[0]), imgfpath.with_suffix(".trace.preds.pt"))
    trace_post_processing(imgfpath, imgfpath.with_suffix(".trace.preds.pt"))
    bboxes = [
        p.polygon
        for p in surya.detection.get_and_clean_boxes(
            heatmap, list(reversed(heatmap.shape)), orig_sizes[0]
        )
    ]

    def slice_and_pad_poly(
        image_array: numpy.array, coordinates: list[list[float]]
    ) -> PIL.Image:
        # Draw polygon onto mask
        coordinates = [(corner[0], corner[1]) for corner in coordinates]
        bbox = [
            min([x[0] for x in coordinates]),
            min([x[1] for x in coordinates]),
            max([x[0] for x in coordinates]),
            max([x[1] for x in coordinates]),
        ]

        # We mask out anything not in the polygon
        cropped_polygon = image_array[bbox[1] : bbox[3], bbox[0] : bbox[2]].copy()
        coordinates = [(x - bbox[0], y - bbox[1]) for x, y in coordinates]

        # Pad the area outside the polygon with the pad value
        mask = numpy.zeros(cropped_polygon.shape[:2], dtype=numpy.uint8)
        cv2.fillPoly(mask, [numpy.int32(coordinates)], 1)
        mask = numpy.stack([mask] * 3, axis=-1)

        cropped_polygon[mask == 0] = 255  # settings.RECOGNITION_PAD_VALUE
        return PIL.Image.fromarray(cropped_polygon)

    image_array = numpy.array(img, dtype=numpy.uint8)
    slices = [slice_and_pad_poly(image_array, poly) for poly in bboxes]

    rec_predictions, confidence_scores = surya.recognition.batch_recognition(
        slices,
        ([["en"]] * len(slices)),
        recognition_model,
        recognition_processor,
        batch_size=32,
    )
    print(list(zip(rec_predictions, confidence_scores)))


class MyDetectorModel(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.model = surya.models.load_predictors()["detection"]

    def forward(self, input):
        return self.model(input).logits


class MyRecognitionEncoderModel(torch.nn.Module):
    def __init__(self):
        super().__init__()
        self.model = surya.models.recognition.model.load_model().encoder

    def forward(self, input):
        return self.model(input).logits


def jit_detector(trace: Path) -> None:
    model = MyDetectorModel()
    traced_model = torch.compile(model)

    # model_graph = torch.fx.symbolic_trace(model)
    # traced_model = torch.jit.script(
    #    model_graph, [torch.rand([2, 3, 1200, 1200], dtype=torch.float32)]
    # )
    torch.save(traced_model, trace.as_posix())


def jit_recognition(trace: Path) -> None:
    model = surya.models.load_predictors()["recognition"]
    traced_model = torch.compile(model)
    torch.save(traced_model, trace.as_posix())


def is_in_venv():
    return sys.prefix != sys.base_prefix


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Surya OCR debug helper")
    parser.add_argument(
        "--venv",
        type=Path,
        default=Path("./venv"),
        help="venv to use",
    )
    parser.add_argument(
        "--jit-detector",
        type=Path,
        default="text_detection.pt",
        help="Generate JIT'ed torchscript model for detector",
    )
    parser.add_argument(
        "--jit-recognition",
        type=Path,
        default="recognition.pt",
        help="Generate JIT'ed torchscript model for recognition",
    )
    parser.add_argument("-i", "--image", type=Path, nargs="*", help="Process Image")
    parser.add_argument(
        "-p",
        "--process-predictions",
        type=Path,
        nargs="*",
        help="Generate BBox and Polygons from predictions",
    )

    args = parser.parse_args()
    if args.jit_detector:
        jit_detector(args.jit_detector)
    if args.jit_recognition:
        jit_recognition(args.jit_recognition)
    for imgf in args.image:
        trace_image(Path(imgf))
        if args.process_predictions:
            _a = [
                trace_post_processing(Path(imgf), Path(arg))
                for arg in args.process_predictions
            ]

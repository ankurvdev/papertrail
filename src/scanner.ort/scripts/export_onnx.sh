#!/usr/bin/env bash
# Rebuild the ONNX models from the original PaddlePaddle checkpoints
# (= what download_models.sh fetches from the *_onnx repos).
#   PaddlePaddle/<model>: inference.json + inference.pdiparams (Paddle 3 PIR)
#     --paddle2onnx-->  models/<model>/inference.onnx  (+ inference.yml: preprocessing, labels, charset)
# Usage: scripts/export_onnx.sh [model ...]
#   default: PP-DocLayoutV3 PP-OCRv6_medium_det PP-OCRv6_medium_rec   (also: PP-OCRv6_{tiny,small}_{det,rec})
set -euo pipefail
cd "$(dirname "$0")/.."
[ $# -gt 0 ] || set -- PP-DocLayoutV3 PP-OCRv6_medium_det PP-OCRv6_medium_rec

VENV=work/venv-onnx
if [ ! -x "$VENV/bin/paddle2onnx" ]; then
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install -q "paddlepaddle==3.3.1" "paddle2onnx==2.1.0" "huggingface_hub==2.4.0"
fi

for m in "$@"; do
  src=work/paddle/$m
  "$VENV/bin/hf" download "PaddlePaddle/$m" --local-dir "$src"
  mkdir -p "models/$m"
  "$VENV/bin/paddle2onnx" --model_dir "$src" \
      --model_filename inference.json --params_filename inference.pdiparams \
      --save_file "models/$m/inference.onnx" --opset_version 17 --enable_onnx_checker True
  cp "$src/inference.yml" "models/$m/"
done

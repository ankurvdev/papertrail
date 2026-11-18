#!/usr/bin/env bash
# Fetch the official, ready-to-run exports into ./models
#   PP-DocLayoutV3 + PP-OCRv6 det/rec (ONNX, Hugging Face)
#   Noto Sans + Noto Sans SC (TTF, Google Fonts) for --overlay
# Usage: scripts/download_models.sh      (OCR_SIZE=tiny|small|medium, default medium)
set -euo pipefail
cd "$(dirname "$0")/.."

SIZE=${OCR_SIZE:-medium}
HF=${HF_ENDPOINT:-https://huggingface.co}
FONTS=https://raw.githubusercontent.com/google/fonts/7af0d90b4d532ffa4b041937b560c6df4e7b1e95/ofl

# repo -> pinned revision (bump deliberately)
declare -A REV=(
  [PP-DocLayoutV3_onnx]=46bbdf188bb0a772c08aed74882ce7e51a8f1ea6
  [PP-OCRv6_tiny_det_onnx]=2ba1506c0380b8f0b03dd142459aac66d4421f6c
  [PP-OCRv6_tiny_rec_onnx]=2612ab37152ae0a677521bae4e1e3d4fb4cf7c30
  [PP-OCRv6_small_det_onnx]=28fe5895c24fd108c19eb3e8479f4ab385fbfc62
  [PP-OCRv6_small_rec_onnx]=b8f84f0b80c529de40b4fbb3544b84fa7233a513
  [PP-OCRv6_medium_det_onnx]=61323801669c338b7891481ec7bac61ce31b576a
  [PP-OCRv6_medium_rec_onnx]=50c7eacafc52fa7bcf4194e8cd08e46f8558504b
)

get() {  # <url> <dest>
  [ -s "$2" ] && { echo "have $2"; return; }
  mkdir -p "$(dirname "$2")"
  echo "get  $2"
  curl -fL --retry 3 --progress-bar -o "$2.part" "$1"
  mv "$2.part" "$2"
}
hf() { get "$HF/PaddlePaddle/$1/resolve/${REV[$1]}/$2" "$3"; }  # <repo> <file> <dest>

for m in PP-DocLayoutV3 "PP-OCRv6_${SIZE}_det" "PP-OCRv6_${SIZE}_rec"; do
  for f in inference.onnx inference.yml; do hf "${m}_onnx" "$f" "models/$m/$f"; done
done

get "$FONTS/notosans/NotoSans%5Bwdth%2Cwght%5D.ttf" models/fonts/NotoSans.ttf    # Latin, Greek, Cyrillic
get "$FONTS/notosanssc/NotoSansSC%5Bwght%5D.ttf"    models/fonts/NotoSansSC.ttf  # CJK fallback
get "$FONTS/notosans/OFL.txt"                       models/fonts/OFL.txt

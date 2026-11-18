set -Eoux pipefail
python3.12 -m ensurepip --upgrade
python3.12 -m pip install --upgrade pip
uv python pin 3.12
uv pip install torch --index-url https://download.pytorch.org/whl/cpu --system --python 3.12
uv pip install paddlepaddle paddleocr pnnx --system --python 3.12
TMP_PY="$(mktemp)"
cat > "$TMP_PY" <<'PY'
from pathlib import Path
from paddlex import create_model
import hashlib
import json
import shutil
import sys
import os

models = [
    "PP-DocLayout-S",
    "PP-DocLayout-M",
    "PP-DocLayout-L",
    "PP-LCNet_x0_25_textline_ori",
    "PP-OCRv5_mobile_det",
    "PP-OCRv5_mobile_rec",
    "PP-OCRv5_server_det",
    "PP-OCRv5_server_rec",

    "en_PP-OCRv5_mobile_rec",
    "devanagari_PP-OCRv5_mobile_rec",

    #"latin_PP-OCRv5_mobile_rec",
    #"eslav_PP-OCRv5_mobile_rec",
    #"cyrillic_PP-OCRv5_mobile_rec",
    #"th_PP-OCRv5_mobile_rec",
    #"arabic_PP-OCRv5_mobile_rec",
    #"korean_PP-OCRv5_mobile_rec",
    #"el_PP-OCRv5_mobile_rec",
]

out = Path(os.environ["BASE_DIR"]).expanduser().resolve()
out.mkdir(parents=True, exist_ok=True)

manifest = []

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

for name in models:
    print(f"\n=== Downloading {name} ===", flush=True)
    create_model(name)

    src = Path.home() / ".paddlex" / "official_models" / name
    if not src.exists():
        print(f"ERROR: expected model dir not found: {src}", file=sys.stderr)
        sys.exit(1)

    dst = out / name
    if dst.exists():
        shutil.rmtree(dst)

    shutil.copytree(src, dst)

    files = []
    total_size = 0

    for p in sorted(dst.rglob("*")):
        if not p.is_file():
            continue

        size = p.stat().st_size
        total_size += size

        files.append({
            "path": str(p.relative_to(out)),
            "size": size,
            "sha256": sha256(p),
        })

    manifest.append({
        "model": name,
        "paddlex_cache": str(src),
        "copied_to": str(dst),
        "total_size": total_size,
        "files": files,
    })

manifest_path = out / "manifest.json"
manifest_path.write_text(
    json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
    encoding="utf-8",
)

sha_path = out / "SHA256SUMS"
with sha_path.open("w", encoding="utf-8") as f:
    for item in manifest:
        for file in item["files"]:
            f.write(f'{file["sha256"]}  {file["path"]}\n')

summary_path = out / "SUMMARY.txt"
with summary_path.open("w", encoding="utf-8") as f:
    for item in manifest:
        mib = item["total_size"] / 1024 / 1024
        f.write(f'{item["model"]}: {item["total_size"]} bytes ({mib:.2f} MiB)\n')

print("\nDone.")
print(f"Manifest: {manifest_path}")
print(f"SHA256:   {sha_path}")
print(f"Summary:  {summary_path}")
PY

BASE_DIR=/output/models
BASE_DIR=${BASE_DIR} python3.12 "$TMP_PY"
rm -f "$TMP_PY"

echo ""
echo "Downloaded model directories:"
find "$BASE_DIR" -maxdepth 1 -type d -not -path "$BASE_DIR" -print | sort

echo ""
echo "Model size summary:"
cat "$BASE_DIR/SUMMARY.txt"

echo ""
echo "Total size:"
du -sh "$BASE_DIR"
paddlex --install paddle2onnx
#paddleocr ocr -i /app/test.png
#paddleocr ocr -i /app/test.png --text_recognition_model_name PP-OCRv5_mobile_det --model_name PP-OCRv5_mobile_rec
#paddleocr ocr -i /app/test.png --text_recognition_model_name PP-OCRv5_server_det --model_name PP-OCRv5_server_rec

#paddle2onnx --model_dir ${BASE_DIR}/en_PP-OCRv5_mobile_det -model--save_file ${BASE_DIR}/en_PP-OCRv5_mobile_det/inference.onnx
#paddle2onnx --model_dir ${BASE_DIR}/en_PP-OCRv5_mobile_rec --save_file ${BASE_DIR}/en_PP-OCRv5_mobile_rec/inference.onnx

paddlex --paddle2onnx --paddle_model_dir ${BASE_DIR}/PP-OCRv5_server_det --onnx_model_dir ${BASE_DIR}/PP-OCRv5_server_det 
paddlex --paddle2onnx --paddle_model_dir ${BASE_DIR}/PP-OCRv5_server_rec --onnx_model_dir ${BASE_DIR}/PP-OCRv5_server_rec
paddlex --paddle2onnx --paddle_model_dir ${BASE_DIR}/PP-OCRv5_mobile_det --onnx_model_dir ${BASE_DIR}/PP-OCRv5_mobile_det 
paddlex --paddle2onnx --paddle_model_dir ${BASE_DIR}/PP-OCRv5_mobile_rec --onnx_model_dir ${BASE_DIR}/PP-OCRv5_mobile_rec
paddlex --paddle2onnx --paddle_model_dir ${BASE_DIR}/PP-LCNet_x0_25_textline_ori --onnx_model_dir ${BASE_DIR}/PP-LCNet_x0_25_textline_ori

pnnx ${BASE_DIR}/PP-OCRv5_mobile_det/inference.onnx inputshape=[1,3,320,320] inputshape2=[1,3,256,256]
pnnx ${BASE_DIR}/PP-OCRv5_mobile_rec/inference.onnx inputshape=[1,3,48,160] inputshape2=[1,3,48,256]

pnnx ${BASE_DIR}/PP-OCRv5_server_det/inference.onnx inputshape=[1,3,320,320] inputshape2=[1,3,256,256] fp16=0
pnnx ${BASE_DIR}/PP-OCRv5_server_rec/inference.onnx inputshape=[1,3,48,160] inputshape2=[1,3,48,256] fp16=0

pnnx ${BASE_DIR}/PP-LCNet_x0_25_textline_ori/inference.onnx inputshape=[1,3,80,160]


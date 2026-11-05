#!/usr/bin/env bash
# Downloads the model runtime and the pretrained vehicle model. Nothing here goes into git.
# Works in Git Bash on Windows and in bash on Linux. Run from anywhere.
#   ONNX Runtime: MIT licence, github.com/microsoft/onnxruntime
#   YOLOX:        Apache-2.0 licence, github.com/Megvii-BaseDetection/YOLOX
#   Plate models: for trials only, see the licence notes in README.md
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ort_version="1.31.1"

case "$(uname -s)" in
    Linux*)
        ort_name="onnxruntime-linux-x64-${ort_version}"
        ort_file="${ort_name}.tgz"
        ort_sha="c849e36a1ef3e22d4ff217b9d1ce4a3cf5b9467b975610913ae959ad5b16bdc9"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        ort_name="onnxruntime-win-x64-${ort_version}"
        ort_file="${ort_name}.zip"
        ort_sha="5869e26bb25314375f2d5ef1877e3ea06b1543d6c710e6e4a1ec2f7177ab4b2c"
        ;;
    *) echo "Unsupported system: $(uname -s)" >&2; exit 1 ;;
esac

fetch() { # url target sha256
    local url="$1" target="$2" sha="$3"
    if [[ ! -f "$target" ]]; then
        mkdir -p "$(dirname "$target")"
        curl --fail --location --silent --show-error --output "$target.part" "$url"
        mv "$target.part" "$target"
    fi
    if [[ -n "$sha" ]]; then
        echo "$sha  $target" | sha256sum --check --quiet || { rm -f "$target"; exit 1; }
    else
        sha256sum "$target"
    fi
}

ort_dir="$root/third_party/$ort_name"
if [[ ! -f "$ort_dir/include/onnxruntime_c_api.h" ]]; then
    archive="$root/third_party/downloads/$ort_file"
    fetch "https://github.com/microsoft/onnxruntime/releases/download/v${ort_version}/${ort_file}" "$archive" "$ort_sha"
    case "$ort_file" in
        *.zip) (cd "$root/third_party" && unzip -q -o "downloads/$ort_file") ;;
        *)     tar -xzf "$archive" -C "$root/third_party" ;;
    esac
fi

fetch "https://github.com/Megvii-BaseDetection/YOLOX/releases/download/0.1.1rc0/yolox_s.onnx" \
      "$root/models/yolox_s.onnx" "c5c2d13e59ae883e6af3b45daea64af4833a4951c92d116ec270d9ddbe998063"
fetch "https://raw.githubusercontent.com/Megvii-BaseDetection/YOLOX/main/assets/dog.jpg" \
      "$root/models/samples/dog.jpg" "5a9522051c3cec2bbd2f6323fccba32e8fbf3ddcc2b3e2fd46b04c720bc6f866"

ocr="https://github.com/ankandrew/fast-plate-ocr/releases/download/arg-plates"
fetch "$ocr/cct_s_v2_global.onnx" "$root/models/cct_s_v2_global.onnx" \
      "384bbbd2cea3ef54761d3df70822ef3a349ee1a112aeafddbe0e3ba06bc6e47b"
fetch "$ocr/cct_s_v2_global_plate_config.yaml" "$root/models/cct_s_v2_global_plate_config.yaml" \
      "0335c74a305173bb6f393efed0fde03cadeaa0b649ed8e19f431016d8232d0a6"
fetch "https://github.com/ankandrew/open-image-models/releases/download/assets/yolo-v9-t-640-license-plates-end2end.onnx" \
      "$root/models/yolo-v9-t-640-license-plates-end2end.onnx" ""

echo "ONNX Runtime: $ort_dir"

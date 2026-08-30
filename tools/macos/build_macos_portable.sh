#!/bin/bash
set -euo pipefail

architecture="${1:-arm64}"
bootstrap_python="${2:-python3}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "This builder must run on macOS." >&2
    exit 2
fi
if [[ "$architecture" != "arm64" && "$architecture" != "x86_64" ]]; then
    echo "Usage: $0 [arm64|x86_64] [path-to-Python-3.11]" >&2
    exit 2
fi

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_root="$(cd "$script_dir/../.." && pwd)"
workspace_root="$(cd "$project_root/../.." && pwd)"
build_root="$project_root/build/macos-$architecture"
runtime_build="$build_root/runtime"
venv="$build_root/packaging-venv"
dist_root="$project_root/dist"
if [[ "$architecture" == "arm64" ]]; then
    platform_name="macOS Apple Silicon"
    deployment_target="12.3"
    expected_backend="mps"
else
    platform_name="macOS Intel"
    deployment_target="10.15"
    expected_backend="cpu"
fi
portable_root="$dist_root/Music SSP FX Portable $platform_name"
zip_path="$dist_root/HTDemucs_GPU_FX_Standalone_${architecture}.zip"

bootstrap_command=("$bootstrap_python")
python_version="$("${bootstrap_command[@]}" -c 'import platform,sys; print(f"{sys.version_info.major}.{sys.version_info.minor} {platform.machine()}")' 2>/dev/null || true)"
use_rosetta_python=0
if [[ "$python_version" != "3.11 $architecture" &&
      "$architecture" == "x86_64" && "$(uname -m)" == "arm64" ]]; then
    rosetta_version="$(/usr/bin/arch -x86_64 "$bootstrap_python" -c 'import platform,sys; print(f"{sys.version_info.major}.{sys.version_info.minor} {platform.machine()}")' 2>/dev/null || true)"
    if [[ "$rosetta_version" == "3.11 x86_64" ]]; then
        bootstrap_command=(/usr/bin/arch -x86_64 "$bootstrap_python")
        python_version="$rosetta_version"
        use_rosetta_python=1
    fi
fi
if [[ "$python_version" != "3.11 $architecture" ]]; then
    cat >&2 <<EOF
The builder needs native Python 3.11 for $architecture, but received: $python_version
For Intel on Apple Silicon, install an x86_64 Python 3.11 and run this builder
from a Rosetta shell. The app itself does not require Python after packaging.
EOF
    exit 3
fi

for required in \
    "$project_root/assets/models/model-manifest.json" \
    "$workspace_root/work/demucs/demucs/states.py" \
    "$project_root/third_party/JUCE/CMakeLists.txt"; do
    [[ -e "$required" ]] || { echo "Required source is missing: $required" >&2; exit 4; }
done

mkdir -p "$build_root" "$dist_root"

stop_token_probe="$build_root/stop-token-probe"
if ! /usr/bin/xcrun clang++ -std=c++20 -arch "$architecture" \
    "-mmacosx-version-min=$deployment_target" -x c++ - -o "$stop_token_probe" <<'CPP'
#include <stop_token>
int main() {
    std::stop_source source;
    return source.stop_possible() ? 0 : 1;
}
CPP
then
    echo "The active Xcode toolchain does not provide C++20 std::stop_token." >&2
    echo "Install a newer Xcode/Command Line Tools release and select it with xcode-select." >&2
    exit 3
fi
rm -f "$stop_token_probe"

if [[ ! -x "$venv/bin/python" ]]; then
    "${bootstrap_command[@]}" -m venv "$venv"
fi
python="$venv/bin/python"
python_command=("$python")
if [[ "$use_rosetta_python" == "1" ]]; then
    python_command=(/usr/bin/arch -x86_64 "$python")
fi
"${python_command[@]}" -m pip install --upgrade "pip==25.1.1"
"${python_command[@]}" -m pip install \
    "pyinstaller==6.16.0" \
    "torch==2.1.2" \
    "numpy==1.26.4" \
    "PyYAML==6.0.1" \
    "tqdm==4.65.0" \
    "einops==0.7.0" \
    "julius==0.2.8" \
    "imageio-ffmpeg==0.6.0"

export HTFX_TARGET_ARCH="$architecture"
"${python_command[@]}" - <<'PY'
import os, platform
import torch

expected = os.environ["HTFX_TARGET_ARCH"]
assert platform.machine() == expected
if expected == "arm64":
    assert torch.backends.mps.is_built(), "PyTorch wheel was not built with MPS"
print("python", platform.python_version(), "torch", torch.__version__, "machine", platform.machine(),
      "mps_built", torch.backends.mps.is_built())
PY

runtime_dist="$runtime_build/dist"
runtime_work="$runtime_build/work"
runtime_spec="$runtime_build/spec"
for target in "$runtime_dist" "$runtime_work" "$runtime_spec"; do
    case "$target" in "$build_root"/*) rm -rf "$target" ;; *) echo "Unsafe build path: $target" >&2; exit 5 ;; esac
done
mkdir -p "$runtime_dist" "$runtime_work" "$runtime_spec"

export PYTHONPATH="$project_root/worker:$project_root/src:$workspace_root/work/demucs${PYTHONPATH:+:$PYTHONPATH}"
"${python_command[@]}" -m PyInstaller \
    --noconfirm --clean --onedir --console \
    --name htdemucs-worker \
    --distpath "$runtime_dist" \
    --workpath "$runtime_work" \
    --specpath "$runtime_spec" \
    --additional-hooks-dir "$project_root/tools/pyinstaller-hooks" \
    --paths "$project_root/worker" \
    --paths "$project_root/src" \
    --paths "$workspace_root/work/demucs" \
    --hidden-import yaml \
    --hidden-import demucs.pretrained \
    --hidden-import demucs.states \
    --hidden-import demucs.apply \
    --hidden-import demucs.htdemucs \
    --collect-submodules einops \
    --collect-submodules julius \
    --exclude-module tensorflow \
    --exclude-module keras \
    --exclude-module matplotlib \
    --exclude-module pandas \
    --exclude-module sklearn \
    --exclude-module scipy \
    --exclude-module pytest \
    --exclude-module setuptools \
    --exclude-module sphinx \
    --exclude-module docutils \
    --exclude-module dask \
    --exclude-module distributed \
    --exclude-module pyarrow \
    --exclude-module numba \
    --exclude-module llvmlite \
    --exclude-module lxml \
    --exclude-module PIL \
    --exclude-module cv2 \
    --exclude-module tkinter \
    --exclude-module PyQt5 \
    --exclude-module transformers \
    --exclude-module datasets \
    --exclude-module spacy \
    --exclude-module torchvision \
    --exclude-module torchaudio \
    --exclude-module xformers \
    --exclude-module torch.utils.tensorboard \
    --exclude-module torch.onnx \
    --exclude-module torch._dynamo \
    --exclude-module torch._inductor \
    "$project_root/worker/gpu_ipc_worker.py"

runtime_root="$runtime_dist/htdemucs-worker"
worker_executable="$runtime_root/htdemucs-worker"
[[ -x "$worker_executable" ]] || { echo "Worker was not created: $worker_executable" >&2; exit 6; }
rm -rf "$runtime_root/_internal/xformers" "$runtime_root/_internal/torch/bin"
"$worker_executable" --help | grep -q -- '--models-dir'
export HTFX_RUNTIME_ROOT="$runtime_root"
"${python_command[@]}" - <<'PY'
import hashlib, json, os, pathlib, platform, torch
import einops, numpy

root = pathlib.Path(os.environ['HTFX_RUNTIME_ROOT'])
worker = root / 'htdemucs-worker'
h = hashlib.sha256(worker.read_bytes()).hexdigest()
manifest = {
    'format': 'PyInstaller onedir',
    'entrypoint': 'htdemucs-worker',
    'entrypoint_sha256': h,
    'runtime_bytes': sum(p.stat().st_size for p in root.rglob('*') if p.is_file()),
    'versions': {
        'python': platform.python_version(),
        'torch': torch.__version__,
        'numpy': numpy.__version__,
        'einops': einops.__version__,
    },
    'external_python_required': False,
    'mps_built': bool(torch.backends.mps.is_built()),
    'cuda_toolkit_required': False,
}
(root / 'runtime-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
PY

cmake -S "$project_root" -B "$build_root/plugin" -G Xcode \
    -DHTFX_BUILD_PLUGIN=ON \
    -DHTFX_STANDALONE_ONLY=ON \
    -DCMAKE_OSX_ARCHITECTURES="$architecture" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target"
cmake --build "$build_root/plugin" --config Release \
    --target HTDemucsGpuFX_Standalone htdemucs_worker_registry_smoke

app_source="$build_root/plugin/HTDemucsGpuFX_artefacts/Release/Standalone/Music SSP FX.app"
[[ -d "$app_source" ]] || { echo "Standalone app was not created: $app_source" >&2; exit 7; }
smoke_source="$(find "$build_root/plugin" -type f -name htdemucs_worker_registry_smoke -perm -111 -print -quit)"
[[ -n "$smoke_source" ]] || { echo "Registry smoke executable was not created" >&2; exit 7; }

case "$portable_root" in "$dist_root"/*) rm -rf "$portable_root" ;; *) echo "Unsafe portable path" >&2; exit 8 ;; esac
mkdir -p "$portable_root/PortableData" "$portable_root/Tools" "$portable_root/Licenses"
/usr/bin/ditto "$app_source" "$portable_root/Music SSP FX.app"
app="$portable_root/Music SSP FX.app"
sidecar="$app/Contents/Resources/sidecar"
mkdir -p "$sidecar/Runtime/htdemucs-worker" "$sidecar/Runtime/ffmpeg/bin" \
    "$sidecar/worker" "$sidecar/src/htdemucs_gpu_fx" "$sidecar/models" \
    "$sidecar/demucs_repo/demucs" "$sidecar/deps/einops" "$sidecar/deps/julius"

/usr/bin/rsync -a --exclude '__pycache__' --exclude '*.pyc' \
    "$project_root/worker/" "$sidecar/worker/"
/usr/bin/rsync -a --exclude '__pycache__' --exclude '*.pyc' \
    "$project_root/src/htdemucs_gpu_fx/" "$sidecar/src/htdemucs_gpu_fx/"
/usr/bin/rsync -a "$project_root/assets/models/" "$sidecar/models/"
/usr/bin/rsync -a --exclude '__pycache__' --exclude '*.pyc' \
    "$workspace_root/work/demucs/demucs/" "$sidecar/demucs_repo/demucs/"

einops_source="$("${python_command[@]}" -c 'import pathlib,einops; print(pathlib.Path(einops.__file__).parent)')"
julius_source="$("${python_command[@]}" -c 'import pathlib,julius; print(pathlib.Path(julius.__file__).parent)')"
/usr/bin/rsync -a --exclude '__pycache__' --exclude '*.pyc' "$einops_source/" "$sidecar/deps/einops/"
/usr/bin/rsync -a --exclude '__pycache__' --exclude '*.pyc' "$julius_source/" "$sidecar/deps/julius/"
/usr/bin/rsync -a "$runtime_root/" "$sidecar/Runtime/htdemucs-worker/"

ffmpeg_source="$("${python_command[@]}" -c 'import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())')"
cp "$ffmpeg_source" "$sidecar/Runtime/ffmpeg/bin/ffmpeg"
"$ffmpeg_source" -L > "$portable_root/Licenses/FFMPEG-LICENSE.txt" 2>&1
chmod 755 "$sidecar/Runtime/ffmpeg/bin/ffmpeg" "$sidecar/Runtime/htdemucs-worker/htdemucs-worker"
cp "$smoke_source" "$portable_root/Tools/htdemucs_worker_registry_smoke"
chmod 755 "$portable_root/Tools/htdemucs_worker_registry_smoke"
cp "$project_root/tools/macos/verify_portable.command" "$portable_root/Verify Music SSP FX.command"
chmod 755 "$portable_root/Verify Music SSP FX.command"
cp "$project_root/packaging/MACOS_PORTABLE_README.txt" "$portable_root/README.txt"
printf '%s\n' "$architecture" > "$portable_root/package-architecture.txt"

cp "$project_root/THIRD_PARTY_NOTICES.md" "$portable_root/Licenses/THIRD_PARTY_NOTICES.md"
cp "$project_root/third_party/JUCE/LICENSE.md" "$portable_root/Licenses/JUCE-LICENSE.md"
cp "$workspace_root/work/demucs/LICENSE" "$portable_root/Licenses/DEMUCS-LICENSE.txt"
for distribution in torch numpy PyYAML tqdm einops julius pyinstaller imageio-ffmpeg; do
    license="$("${python_command[@]}" - "$distribution" <<'PY'
import importlib.metadata as m
import pathlib, sys
p = pathlib.Path(m.distribution(sys.argv[1])._path)
candidates = sorted(
    x for x in p.rglob('*')
    if x.is_file() and x.name.lower().startswith(('license', 'licence', 'copying'))
)
print(candidates[0] if candidates else '')
PY
)"
    if [[ -n "$license" ]]; then
        cp "$license" "$portable_root/Licenses/${distribution//[^A-Za-z0-9]/_}-LICENSE.txt"
    else
        echo "Licence file not found for bundled distribution: $distribution" >&2
        exit 10
    fi
done
python_license="$("${python_command[@]}" - <<'PY'
import pathlib, sys
p = pathlib.Path(sys.base_prefix)
candidates = (
    p / 'LICENSE.txt',
    p / 'LICENSE',
    p / 'Resources' / 'English.lproj' / 'License.rtf',
    p.parent / 'Resources' / 'English.lproj' / 'License.rtf',
)
print(next((x for x in candidates if x.is_file()), ''))
PY
)"
[[ -n "$python_license" ]] || {
    echo "CPython licence was not found under: $("${python_command[@]}" -c 'import sys; print(sys.base_prefix)')" >&2
    exit 10
}
cp "$python_license" "$portable_root/Licenses/PYTHON-LICENSE.txt"

worker_packaged="$sidecar/Runtime/htdemucs-worker/htdemucs-worker"
ffmpeg_packaged="$sidecar/Runtime/ffmpeg/bin/ffmpeg"
app_executable="$app/Contents/MacOS/Music SSP FX"
for binary in "$worker_packaged" "$ffmpeg_packaged" "$app_executable"; do
    /usr/bin/lipo -archs "$binary" | tr ' ' '\n' | grep -qx "$architecture" || {
        echo "Wrong architecture in $binary: $(/usr/bin/lipo -archs "$binary")" >&2
        exit 9
    }
done

version_exceeds() {
    /usr/bin/awk -v actual="$1" -v limit="$2" 'BEGIN {
        split(actual, a, "."); split(limit, b, ".");
        for (i = 1; i <= 3; ++i) {
            av = (a[i] == "" ? 0 : a[i]) + 0;
            bv = (b[i] == "" ? 0 : b[i]) + 0;
            if (av > bv) exit 0;
            if (av < bv) exit 1;
        }
        exit 1;
    }'
}

while IFS= read -r -d '' binary; do
    /usr/bin/file -b "$binary" | /usr/bin/grep -q 'Mach-O' || continue
    min_versions="$(/usr/bin/otool -l "$binary" | /usr/bin/awk '
        $1 == "cmd" && $2 == "LC_BUILD_VERSION" { mode = "build"; next }
        $1 == "cmd" && $2 == "LC_VERSION_MIN_MACOSX" { mode = "legacy"; next }
        mode == "build" && $1 == "minos" { print $2; mode = ""; next }
        mode == "legacy" && $1 == "version" { print $2; mode = ""; next }
    ')"
    [[ -n "$min_versions" ]] || {
        echo "Cannot determine minimum macOS version for: $binary" >&2
        exit 9
    }
    while IFS= read -r min_version; do
        if version_exceeds "$min_version" "$deployment_target"; then
            echo "Bundled binary requires macOS $min_version, above target $deployment_target: $binary" >&2
            echo "Use a Python 3.11 distribution and dependencies built for the stated deployment target." >&2
            exit 9
        fi
    done <<< "$min_versions"
done < <(/usr/bin/find "$app" "$portable_root/Tools" -type f \( -perm -111 -o -name '*.dylib' -o -name '*.so' \) -print0)

export HTFX_MANIFEST_PORTABLE="$portable_root"
export HTFX_MANIFEST_ARCH="$architecture"
export HTFX_MANIFEST_PLATFORM="$platform_name"
export HTFX_MANIFEST_BACKEND="$expected_backend"
"${python_command[@]}" - <<'PY'
import datetime, hashlib, json, os, pathlib, platform, torch

root = pathlib.Path(os.environ['HTFX_MANIFEST_PORTABLE'])
app = root / 'Music SSP FX.app'
sidecar = app / 'Contents/Resources/sidecar'
models = json.loads((sidecar / 'models/model-manifest.json').read_text())
def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()
expected_model_files = set(models['sha256'])
packaged_model_files = {p.name for p in (sidecar / 'models').glob('*.th')}
if packaged_model_files != expected_model_files:
    raise RuntimeError(
        f'model file set mismatch: packaged={sorted(packaged_model_files)} '
        f'expected={sorted(expected_model_files)}'
    )
for filename, expected in models['sha256'].items():
    actual = sha(sidecar / 'models' / filename)
    if actual != expected:
        raise RuntimeError(
            f'model checksum mismatch for {filename}: {actual} != {expected}'
        )
manifest = {
    'product': 'Music SSP FX Portable Standalone',
    'version': '0.1.0-local-test',
    'platform': os.environ['HTFX_MANIFEST_PLATFORM'],
    'architecture': os.environ['HTFX_MANIFEST_ARCH'],
    'app_bundle': 'Music SSP FX.app',
    'app_executable_sha256': sha(app / 'Contents/MacOS/Music SSP FX'),
    'worker_runtime': 'Music SSP FX.app/Contents/Resources/sidecar/Runtime/htdemucs-worker/htdemucs-worker',
    'worker_sha256': sha(sidecar / 'Runtime/htdemucs-worker/htdemucs-worker'),
    'ffmpeg_runtime': 'Music SSP FX.app/Contents/Resources/sidecar/Runtime/ffmpeg/bin/ffmpeg',
    'ffmpeg_sha256': sha(sidecar / 'Runtime/ffmpeg/bin/ffmpeg'),
    'python_bundled': True,
    'external_python_required': False,
    'torch_version': torch.__version__,
    'runtime_bytes': sum(
        p.stat().st_size
        for p in (sidecar / 'Runtime').rglob('*')
        if p.is_file()
    ),
    'default_compute_backend': os.environ['HTFX_MANIFEST_BACKEND'],
    'mps_built': bool(torch.backends.mps.is_built()),
    'cuda_toolkit_required': False,
    'nvidia_driver_required': False,
    'models': models['models'],
    'model_sha256': models['sha256'],
    'model_bytes': sum(
        (sidecar / 'models' / filename).stat().st_size
        for filename in models['sha256']
    ),
    'default_model': models['default_model'],
    'ffmpeg_bundled': True,
    'settings_directory': 'PortableData',
    'sidecar_directory': 'Music SSP FX.app/Contents/Resources/sidecar',
    'media_import': ['audio', 'video'],
    'media_export': ['32-bit-float-original-volume-stems', 'interface-mix-wav', 'video-with-replaced-mix-mp4'],
    'compute_backends': ['Auto', 'NVIDIA CUDA', 'CPU', 'Apple MPS'],
    'sample_rate_hz': 44100,
    'channels': 2,
    'default_mode': 'Record mode',
    'default_latency_samples': 0,
    'realtime_mode': 'Realtime mode (Ultra high latency)',
    'realtime_default_latency_samples': 366030,
    'segment_seconds': [2.0, 3.0, 4.0, 5.0, 7.8],
    'licence_notices': 'Licenses/THIRD_PARTY_NOTICES.md',
    'packaged_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'redistribution_ready': False,
    'redistribution_blockers': [
        'JUCE licence route',
        'Demucs pretrained-weight rights',
        'product licence/publisher identity',
    ],
}
(root / 'portable-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
PY

codesign_identity="${HTFX_CODESIGN_IDENTITY:--}"
if [[ "$codesign_identity" == "-" ]]; then
    /usr/bin/codesign --force --sign - "$portable_root/Tools/htdemucs_worker_registry_smoke"
    /usr/bin/codesign --force --deep --sign - "$app"
else
    /usr/bin/codesign --force --options runtime --timestamp \
        --sign "$codesign_identity" "$portable_root/Tools/htdemucs_worker_registry_smoke"
    /usr/bin/codesign --force --deep --options runtime --timestamp \
        --sign "$codesign_identity" "$app"
fi
/usr/bin/codesign --verify --strict --verbose=2 "$portable_root/Tools/htdemucs_worker_registry_smoke"
/usr/bin/codesign --verify --deep --strict --verbose=2 "$app"

export HTFX_SIGNED_APP_EXECUTABLE="$app_executable"
"${python_command[@]}" - <<'PY'
import hashlib, json, os, pathlib

manifest_path = pathlib.Path(os.environ['HTFX_MANIFEST_PORTABLE']) / 'portable-manifest.json'
app_executable = pathlib.Path(os.environ['HTFX_SIGNED_APP_EXECUTABLE'])
h = hashlib.sha256()
with app_executable.open('rb') as f:
    for chunk in iter(lambda: f.read(1024 * 1024), b''):
        h.update(chunk)
manifest = json.loads(manifest_path.read_text())
manifest['app_executable_sha256'] = h.hexdigest()
manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
PY

rm -f "$zip_path"
(cd "$dist_root" && /usr/bin/ditto -c -k --sequesterRsrc --keepParent \
    "$(basename "$portable_root")" "$(basename "$zip_path")")
/usr/bin/shasum -a 256 "$zip_path"
echo "portable=$portable_root"
echo "zip=$zip_path"
echo "Run: $portable_root/Verify Music SSP FX.command"

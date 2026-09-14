#!/usr/bin/env bash
# Freeze the Python worker into a self-contained bundle on macOS.
#
# This is the macOS counterpart of build_standalone_runtime.ps1 and keeps the
# same PyInstaller arguments; only the platform-specific parts differ.
#
#   tools/build_standalone_runtime_macos.sh --python /path/to/python3
#
# The architecture is taken from the interpreter, not from a flag, because that
# is the only thing that actually decides what lands in the bundle:
#
#   Apple Silicon (arm64)  a normal arm64 python  -> MPS-capable runtime
#   Intel (x86_64)         an x86_64 python run under Rosetta 2 on the same
#                          Mac -> CPU-only runtime
#
# PyTorch stopped publishing macOS x86_64 wheels after 2.2.2, so the Intel
# runtime is pinned to torch 2.1-2.2 with numpy<2 -- the combination demucs
# itself declares for that platform. The script checks this rather than
# trusting the environment.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python_bin=""
while [ $# -gt 0 ]; do
    case "$1" in
        --python) python_bin="${2:-}"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
if [ -z "$python_bin" ]; then
    python_bin="$(command -v python3 || true)"
fi
[ -x "$python_bin" ] || { echo "python not found: ${python_bin:-<none>}" >&2; exit 1; }

demucs_root="$repo_root/third_party/demucs"
[ -f "$demucs_root/demucs/__init__.py" ] || {
    echo "Demucs submodule is missing. Run: git submodule update --init --recursive" >&2
    exit 1
}

arch="$("$python_bin" -c 'import platform; print(platform.machine())')"
case "$arch" in
    arm64|x86_64) ;;
    *) echo "unsupported interpreter architecture: $arch" >&2; exit 1 ;;
esac
python_tag="$("$python_bin" -c 'import sys; print(f"py{sys.version_info.major}{sys.version_info.minor}")')"

# The interpreter has to be the flavour its architecture can actually use: an
# arm64 build with no MPS is a silently CPU-only GPU runtime, and an x86_64
# build on a too-new torch has no wheel at all.
if [ "$arch" = "arm64" ]; then
    "$python_bin" - <<'PY'
import sys, torch
assert torch.backends.mps.is_available(), "this arm64 torch cannot use MPS"
print(torch.__version__, "mps")
PY
else
    "$python_bin" - <<'PY'
import sys, numpy, torch
major, minor = (int(p) for p in torch.__version__.split(".")[:2])
assert (major, minor) < (2, 3), (
    f"torch {torch.__version__} has no macOS x86_64 wheel; pin torch<2.3")
assert int(numpy.__version__.split(".")[0]) < 2, (
    f"numpy {numpy.__version__} is too new for this torch; pin numpy<2")
print(torch.__version__, numpy.__version__, "cpu-only")
PY
fi

build_root="$repo_root/build"
dist_root="$build_root/standalone-runtime-macos-$arch-dist"
work_root="$build_root/standalone-runtime-macos-$arch-work"
spec_root="$build_root/standalone-runtime-macos-$arch-spec"
tool_root="$build_root/packaging-tools-macos-$arch-$python_tag"
hook_root="$repo_root/tools/pyinstaller-hooks"

# PyInstaller is installed with `pip --target`, so its tree is tied to both the
# interpreter version and its architecture. Keep one tool directory per pair.
if [ ! -f "$tool_root/PyInstaller/__main__.py" ]; then
    echo "installing PyInstaller for $arch/$python_tag into $tool_root ..."
    "$python_bin" -m pip install --disable-pip-version-check --no-warn-script-location \
        --target "$tool_root" pyinstaller
fi

for target in "$dist_root" "$work_root" "$spec_root"; do
    case "$target" in
        "$build_root"/*) ;;
        *) echo "refusing to replace a runtime folder outside $build_root" >&2; exit 1 ;;
    esac
    rm -rf "$target"
    mkdir -p "$target"
done

export PYTHONPATH="$tool_root:$repo_root/worker:$repo_root/src:$demucs_root${PYTHONPATH:+:$PYTHONPATH}"

"$python_bin" -m PyInstaller \
    --noconfirm --clean --onedir --console \
    --name htdemucs-worker \
    --distpath "$dist_root" --workpath "$work_root" --specpath "$spec_root" \
    --additional-hooks-dir "$hook_root" \
    --paths "$repo_root/worker" --paths "$repo_root/src" --paths "$demucs_root" \
    --hidden-import yaml \
    --hidden-import demucs.pretrained \
    --hidden-import demucs.states \
    --hidden-import demucs.apply \
    --hidden-import demucs.htdemucs \
    --hidden-import gpu_ipc_worker \
    `# Checkpoints pickled against NumPy 1.x name numpy.core.*, which NumPy 2` \
    `# keeps only as a compatibility shim that static analysis never sees.` \
    --collect-submodules numpy.core \
    `# MelBand RoFormer shares this bundle so torch is packaged once, not twice.` \
    --hidden-import roformer_worker \
    --hidden-import roformer_cache \
    --hidden-import soundfile \
    --hidden-import librosa \
    --hidden-import ml_collections \
    --hidden-import beartype \
    --hidden-import packaging \
    --collect-submodules mel_band_roformer \
    --collect-submodules rotary_embedding_torch \
    --collect-data mel_band_roformer \
    --collect-submodules einops \
    --collect-submodules julius \
    --exclude-module mlx --exclude-module mlx_spectro \
    --exclude-module tensorflow --exclude-module keras \
    --exclude-module matplotlib --exclude-module pandas --exclude-module sklearn \
    --exclude-module pytest --exclude-module setuptools \
    --exclude-module sphinx --exclude-module docutils \
    --exclude-module dask --exclude-module distributed --exclude-module pyarrow \
    --exclude-module lxml --exclude-module PIL --exclude-module cv2 \
    --exclude-module zmq --exclude-module tkinter --exclude-module PyQt5 \
    --exclude-module cryptography --exclude-module bcrypt --exclude-module nacl \
    --exclude-module transformers --exclude-module datasets \
    --exclude-module spacy --exclude-module thinc --exclude-module pydantic \
    --exclude-module rich --exclude-module botocore --exclude-module boto3 \
    --exclude-module torchvision --exclude-module torchaudio \
    --exclude-module av --exclude-module xformers \
    --exclude-module plotly --exclude-module skimage --exclude-module statsmodels \
    --exclude-module xarray --exclude-module kaleido --exclude-module patsy \
    --exclude-module paramiko --exclude-module selenium --exclude-module bokeh \
    --exclude-module sqlalchemy --exclude-module h5py --exclude-module cloudpickle \
    --exclude-module fsspec --exclude-module lz4 \
    --exclude-module torch.utils.tensorboard --exclude-module torch.onnx \
    --exclude-module torch._dynamo --exclude-module torch._inductor \
    --exclude-module pkg_resources --exclude-module traitlets --exclude-module pygments \
    --exclude-module py --exclude-module IPython --exclude-module notebook \
    --exclude-module jupyter \
    "$repo_root/worker/worker_main.py"

runtime_root="$dist_root/htdemucs-worker"
worker="$runtime_root/htdemucs-worker"
[ -x "$worker" ] || { echo "self-contained worker was not created: $worker" >&2; exit 1; }

# Pulled in by binary probing on broad environments; nothing here imports them.
for relative in _internal/xformers _internal/torch/bin; do
    rm -rf "${runtime_root:?}/$relative"
done

# pip records the builder machine's own path in direct_url.json for locally
# installed dependencies. No runtime code reads it, so drop it before shipping.
find "$runtime_root" -name direct_url.json -type f -delete

if grep -rIl --include='*.json' --include='*.txt' --include='*.md' \
    --include='*.xml' --include='*.yaml' -E '/Users/' "$runtime_root" >/tmp/htfx-identity.$$ 2>/dev/null &&
    [ -s /tmp/htfx-identity.$$ ]; then
    echo "runtime contains machine-identifying text:" >&2
    cat /tmp/htfx-identity.$$ >&2
    rm -f /tmp/htfx-identity.$$
    exit 1
fi
rm -f /tmp/htfx-identity.$$

# The frozen bundle has to start and dispatch both backends before it is worth
# packaging: an import that only fails once frozen is the whole reason this
# check exists.
"$worker" --help | grep -q -- '--models-dir' || {
    echo "self-contained worker import/startup check failed" >&2; exit 1; }
"$worker" roformer --help | grep -q -- '--models-dir' || {
    echo "RoFormer dispatch check failed" >&2; exit 1; }

runtime_bytes="$(find "$runtime_root" -type f -exec stat -f %z {} + | awk '{t+=$1} END {print t}')"
versions="$("$python_bin" -c 'import json,sys,torch,numpy,demucs,einops,mel_band_roformer; print(json.dumps({"python":sys.version.split()[0],"torch":torch.__version__,"cuda":torch.version.cuda,"numpy":numpy.__version__,"demucs":getattr(demucs,"__version__","bundled"),"einops":einops.__version__,"mel_band_roformer":getattr(mel_band_roformer,"__version__","unknown")}))')"
worker_sha="$(shasum -a 256 "$worker" | cut -d' ' -f1)"

"$python_bin" - "$runtime_root/runtime-manifest.json" "$arch" "$worker_sha" \
    "$runtime_bytes" "$versions" <<'PY'
import json, sys
path, arch, sha, size, versions = sys.argv[1:6]
json.dump({
    "format": "PyInstaller onedir",
    "flavor": "mps" if arch == "arm64" else "cpu",
    "platform": "macos",
    "architecture": arch,
    "entrypoint": "htdemucs-worker",
    "backends": ["htdemucs", "roformer"],
    "entrypoint_sha256": sha,
    "runtime_bytes": int(size),
    "versions": json.loads(versions),
    "external_python_required": False,
    "cuda_toolkit_required": False,
    "nvidia_driver_required_for_cuda": False,
}, open(path, "w"), indent=4)
PY

echo "runtime=$runtime_root"
echo "architecture=$arch"
echo "bytes=$runtime_bytes"
echo "worker_sha256=$worker_sha"

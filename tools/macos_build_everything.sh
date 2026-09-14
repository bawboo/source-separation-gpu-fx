#!/usr/bin/env bash
# 在 Mac 上把整個 App 建起來，從零到可以雙擊執行。
#
#   tools/macos_build_everything.sh              # 兩種架構都做（Apple Silicon + Intel）
#   tools/macos_build_everything.sh --arm64-only # 只做 Apple Silicon（比較快）
#
# 這支腳本會依序做完下面每一件事，中途缺什麼會直接告訴你要裝什麼：
#   1. 檢查工具（Xcode 命令列工具、cmake、7-Zip、conda、Rosetta）
#   2. 建立 Python 環境（Apple Silicon 一個、Intel 一個）
#   3. 建置 LGPL FFmpeg
#   4. 凍結 worker runtime
#   5. 封裝 runtime
#   6. 建置 .app 並放進 runtime、ad-hoc 簽章
#
# 每一步都會印出「[n/6]」，中斷後重跑會跳過已完成的部分。
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# 與 Windows runtime 內的 melband-roformer-infer 0.1.6 同一個 commit。
# PyPI 上只發布到 0.1.5，所以這個套件只能從 GitHub 裝。
ROFORMER_COMMIT=77ff05e6ce533d85439d1e7a52d8316a2987c1b9
arches=(arm64 x86_64)
version="dev"
while [ $# -gt 0 ]; do
    case "$1" in
        --arm64-only) arches=(arm64) ;;
        --version) version="${2:-dev}"; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

step() { echo; echo "=== [$1/6] $2"; }
die() { echo; echo "!! $1" >&2; exit 1; }

# ---------------------------------------------------------------- 1. 工具
step 1 "檢查工具"
xcode-select -p >/dev/null 2>&1 || die "缺少 Xcode 命令列工具。請執行：xcode-select --install"
command -v cmake >/dev/null || die "缺少 cmake。請執行：brew install cmake"
command -v 7zz >/dev/null || command -v 7z >/dev/null ||
    die "缺少 7-Zip。請執行：brew install sevenzip"
command -v conda >/dev/null || die "缺少 conda。請先安裝 Miniforge 或 Miniconda"
if [ ${#arches[@]} -gt 1 ] && ! /usr/bin/pgrep -q oahd 2>/dev/null; then
    echo "Rosetta 2 似乎沒安裝；Intel 版需要它。"
    echo "請執行：softwareupdate --install-rosetta --agree-to-license"
    die "裝好 Rosetta 後再重跑，或改用 --arm64-only 只做 Apple Silicon 版"
fi
[ -f "$repo_root/third_party/demucs/demucs/__init__.py" ] ||
    die "demucs submodule 沒有取得。請執行：git submodule update --init --recursive"
echo "工具齊全。"

# ------------------------------------------------------------ 2. Python 環境
step 2 "建立 Python 環境"
env_for_arch() { [ "$1" = arm64 ] && echo htfx-macos-arm64 || echo htfx-macos-x86; }
python_for_arch() { conda run -n "$(env_for_arch "$1")" python -c 'import sys; print(sys.executable)'; }

for arch in "${arches[@]}"; do
    env_name="$(env_for_arch "$arch")"
    if conda env list | awk '{print $1}' | grep -qx "$env_name"; then
        echo "$env_name 已存在，略過。"
        continue
    fi
    echo "建立 $env_name（$arch）..."
    if [ "$arch" = x86_64 ]; then
        # 用 Rosetta 跑 x86_64 的 Python；torch 沒有新版的 Intel mac wheel，
        # 所以這裡必須釘在 2.2 這一代，numpy 也要跟著退回 1.x。
        CONDA_SUBDIR=osx-64 conda create -n "$env_name" python=3.11 -y
        conda run -n "$env_name" conda config --env --set subdir osx-64
        conda run -n "$env_name" python -m pip install \
            'torch<2.3' 'numpy<2' demucs einops soundfile librosa ml_collections beartype
    else
        conda create -n "$env_name" python=3.11 -y
        conda run -n "$env_name" python -m pip install \
            torch numpy demucs einops soundfile librosa ml_collections beartype
    fi
    echo "安裝 RoFormer 推論套件（釘住 commit）..."
    conda run -n "$env_name" python -m pip install \
        "git+https://github.com/openmirlab/melband-roformer-infer.git@$ROFORMER_COMMIT"
    conda run -n "$env_name" python -c 'import mel_band_roformer' >/dev/null 2>&1 || {
        echo
        echo "注意：$env_name 裡的 mel_band_roformer 無法 import。"
        echo "HTDemucs 4/6 軌不受影響，但 RoFormer 的分離模式會無法使用。"
    }
done

# ------------------------------------------------------------- 3~5. 每個架構
for arch in "${arches[@]}"; do
    python_bin="$(python_for_arch "$arch")"

    step 3 "建置 LGPL FFmpeg（$arch）"
    if [ -x "$repo_root/build/ffmpeg-lgpl-macos-$arch/bin/ffmpeg" ]; then
        echo "已存在，略過。"
    else
        "$repo_root/tools/build_ffmpeg_lgpl_macos.sh" --arch "$arch"
    fi

    step 4 "凍結 worker runtime（$arch）"
    # runtime-manifest.json is the only honest completion marker: the freeze
    # script writes it last, after the import and dispatch checks pass. The
    # executable appears long before that, so testing for it makes a failed
    # freeze look finished and hands the next step a broken runtime.
    if [ -f "$repo_root/build/standalone-runtime-macos-$arch-dist/htdemucs-worker/runtime-manifest.json" ]; then
        echo "已存在，略過。"
    else
        "$repo_root/tools/build_standalone_runtime_macos.sh" --python "$python_bin"
    fi

    step 5 "封裝 runtime（$arch）"
    "$repo_root/tools/package_macos_runtime.sh" --arch "$arch" --version "$version" \
        --ffmpeg "$repo_root/build/ffmpeg-lgpl-macos-$arch/bin"
done

# ----------------------------------------------------------------- 6. .app
step 6 "建置 .app"
"$repo_root/tools/build_macos.sh" --bundle-runtime

app="$repo_root/build/macos/HTDemucsGpuFX_artefacts/Release/Standalone/Music SSP FX.app"
echo
echo "================================================================"
echo "完成。App 在這裡："
echo "  $app"
echo
echo "直接雙擊就能執行。第一次分離時會自動下載模型。"
if [ ${#arches[@]} -gt 1 ]; then
    echo
    echo "註：.app 裡放的是這台機器（$(uname -m)）的 runtime。"
    echo "另一個架構的 runtime 已封裝在 dist/macos/，要做給 Intel 機器用的"
    echo "版本時再換進去。"
fi
echo "================================================================"

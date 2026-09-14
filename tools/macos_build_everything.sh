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
#   6. 建置 .app 並放進 runtime、ad-hoc 簽章，最後壓成可以給別人的 DMG
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

# Running out halfway leaves a half-written dist behind an error that says
# nothing about disk, so check first -- but check for what this run will
# actually build. A flat threshold gets this backwards: the cheapest run,
# where every expensive artifact is already there to reuse, is the one with
# the least free space left to satisfy it. These are the same existence tests
# steps 3 and 4 use to decide what to skip, so the estimate cannot drift away
# from what the steps do. Sizes measured on the development Mac and rounded up.
need_gb=2  # the .app, and a DMG per architecture
for arch in "${arches[@]}"; do
    [ -x "$repo_root/build/ffmpeg-lgpl-macos-$arch/bin/ffmpeg" ] || need_gb=$((need_gb + 1))
    [ -f "$repo_root/build/standalone-runtime-macos-$arch-dist/htdemucs-worker/runtime-manifest.json" ] ||
        need_gb=$((need_gb + 2))
done
avail_gb=$(( $(df -k "$repo_root" | awk 'NR==2 {print $4}') / 1024 / 1024 ))
[ "$avail_gb" -ge "$need_gb" ] ||
    die "磁碟空間不足：這次要建的東西約需 ${need_gb} GB，目前只剩 ${avail_gb} GB。"
echo "工具齊全，磁碟剩 ${avail_gb} GB（這次約需 ${need_gb} GB）。"

# ------------------------------------------------------------ 2. Python 環境
step 2 "建立 Python 環境"
env_for_arch() { [ "$1" = arm64 ] && echo htfx-macos-arm64 || echo htfx-macos-x86; }
python_for_arch() { conda run -n "$(env_for_arch "$1")" python -c 'import sys; print(sys.executable)'; }

# An environment that exists is not an environment that works. The previous
# check looked only for the name, so an env left half-built by a run that died
# during pip -- or made by hand -- was skipped with its packages still missing,
# and the gap did not surface until the freeze failed much later. Ask the
# interpreter instead.
# demucs is deliberately absent from this list. The build uses the vendored
# tree, which only the freeze script puts on sys.path, and the x86_64 env does
# not install the PyPI package at all -- so importing it here would report
# "incomplete" on a complete environment and reinstall on every run.
env_is_ready() {
    conda run -n "$1" python -c \
        'import torch, numpy, einops, soundfile, librosa, mel_band_roformer' \
        >/dev/null 2>&1
}

for arch in "${arches[@]}"; do
    env_name="$(env_for_arch "$arch")"
    if conda env list | awk '{print $1}' | grep -qx "$env_name"; then
        if env_is_ready "$env_name"; then
            echo "$env_name 已存在且套件齊全，略過。"
            continue
        fi
        echo "$env_name 已存在但套件不齊，補裝缺少的部分..."
    else
        echo "建立 $env_name（$arch）..."
    fi
    if [ "$arch" = x86_64 ]; then
        # 用 Rosetta 跑 x86_64 的 Python；torch 沒有新版的 Intel mac wheel，
        # 所以這裡必須釘在 2.2 這一代，numpy 也要跟著退回 1.x。
        conda env list | awk '{print $1}' | grep -qx "$env_name" ||
            CONDA_SUBDIR=osx-64 conda create -n "$env_name" python=3.11 -y
        conda run -n "$env_name" conda config --env --set subdir osx-64
        # A wheel for the wrong architecture installs without complaint and
        # only fails once frozen, so make the interpreter say what it is.
        conda run -n "$env_name" python -c \
            'import platform, sys; sys.exit(0 if platform.machine() == "x86_64" else 1)' ||
            die "$env_name 不是 x86_64 環境，CONDA_SUBDIR 沒有生效。先刪掉再重跑：conda env remove -n $env_name"
        # The PyPI demucs package is NOT installed here, and that is not an
        # oversight: demucs 4.1.0 requires sphn>=0.1.12, whose newest osx-64
        # wheel is 0.1.4. sphn is only used by demucs/api.py, which the freeze
        # does not touch -- it needs pretrained/states/apply/htdemucs from the
        # vendored tree -- so the dependencies demucs actually uses are listed
        # directly instead. numba is pinned for the same reason: pip resolves
        # to versions that have no osx-64 wheel at all.
        # scipy is pinned for the machines this build exists to serve. From
        # 1.14 its wheels are stamped minos 14.0, which would put the floor at
        # Sonoma -- and Sonoma dropped every Intel Mac older than 2018, so the
        # Intel build would refuse to run on much of the hardware that still
        # needs it. 1.13.1 is stamped 10.9 and librosa only asks for >=1.6.
        conda run -n "$env_name" python -m pip install \
            'torch<2.3' 'numpy<2' 'numba<0.63' 'scipy<1.14' einops soundfile \
            librosa ml_collections beartype tqdm julius lameenc openunmix \
            dora-search
    else
        conda env list | awk '{print $1}' | grep -qx "$env_name" ||
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

# ----------------------------------------------------------- 6. .app 與 DMG
app="$repo_root/build/macos/HTDemucsGpuFX_artefacts/Release/Standalone/Music SSP FX.app"
dmgs=()
for arch in "${arches[@]}"; do
    step 6 "建置 .app 與 DMG（$arch）"
    # JUCE builds every architecture to the same path, so the previous one has
    # to go before the next is staged. Leaving that to whoever runs this is how
    # an "Intel" bundle ends up carrying the arm64 runtime; the DMG script
    # checks for exactly that, but the check should never be the thing that
    # catches it.
    rm -rf "$app"
    "$repo_root/tools/build_macos.sh" --bundle-runtime --runtime-arch "$arch"
    "$repo_root/tools/package_macos_dmg.sh" --arch "$arch" --version "$version"
    dmgs+=("$repo_root/dist/macos/MusicSSPFX-$version-$arch.dmg")
done

echo
echo "================================================================"
echo "完成。要給別人的檔案在這裡："
for dmg in "${dmgs[@]}"; do
    echo "  $dmg"
done
echo
echo "DMG 裡有 App、Applications 捷徑，和一份「請先讀我.txt」，"
echo "說明第一次開啟要怎麼通過 macOS 的安全檢查。"
if [ ${#arches[@]} -gt 1 ]; then
    echo
    # Not ${arches[-1]}: macOS still ships bash 3.2, where a negative index is
    # a syntax error, and this script's shebang finds whichever bash is first.
    echo "註：$app 現在放的是最後做的那個架構"
    echo "（${arches[$(( ${#arches[@]} - 1 ))]}）。兩種架構共用這個路徑，要單獨重做"
    echo "其中一個的話用 tools/build_macos.sh --bundle-runtime --runtime-arch <arch>。"
fi
echo "================================================================"

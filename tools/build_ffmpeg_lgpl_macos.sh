#!/usr/bin/env bash
# Build a static LGPL FFmpeg for macOS.
#
#   tools/build_ffmpeg_lgpl_macos.sh --arch arm64
#   tools/build_ffmpeg_lgpl_macos.sh --arch x86_64     # cross-compiles on an M-series Mac
#
# Why this exists rather than `brew install ffmpeg`:
#   * Homebrew's FFmpeg is a GPL build (--enable-gpl), and shipping one would
#     put this project under the GPL's corresponding-source obligation. See
#     THIRD_PARTY_NOTICES.md, which settled on LGPL only.
#   * Homebrew's binaries link against dylibs in its own prefix, so they stop
#     working the moment they are copied to another Mac.
#   * The Windows build comes from BtbN/FFmpeg-Builds, which publishes no
#     macOS assets.
#
# FFmpeg is LGPL unless configure is given --enable-gpl, which this never does.
# --disable-autodetect also keeps any library that happens to be installed on
# the build machine out of the result, so the binaries are self-contained and
# cannot pick up a differently licensed dependency by accident.
set -euo pipefail

FFMPEG_VERSION=8.1.2
# Computed from the tarball actually downloaded from ffmpeg.org; the project
# publishes GPG signatures rather than checksums, so this is pinned here.
FFMPEG_SHA256=464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arch=""
while [ $# -gt 0 ]; do
    case "$1" in
        --arch) arch="${2:-}"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[ -n "$arch" ] || arch="$(uname -m)"
case "$arch" in
    arm64|x86_64) ;;
    *) echo "unsupported architecture: $arch" >&2; exit 1 ;;
esac

prefix="$repo_root/build/ffmpeg-lgpl-macos-$arch"
work="$repo_root/build/ffmpeg-src"
tarball="$work/ffmpeg-$FFMPEG_VERSION.tar.xz"
source_dir="$work/ffmpeg-$FFMPEG_VERSION"
build_dir="$work/build-$arch"

mkdir -p "$work"
if [ ! -f "$tarball" ]; then
    echo "downloading ffmpeg $FFMPEG_VERSION ..."
    curl -fL --retry 3 -o "$tarball.partial" \
        "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
    mv "$tarball.partial" "$tarball"
fi
actual="$(shasum -a 256 "$tarball" | cut -d' ' -f1)"
if [ "$actual" != "$FFMPEG_SHA256" ]; then
    echo "ffmpeg tarball checksum mismatch" >&2
    echo "  expected $FFMPEG_SHA256" >&2
    echo "  actual   $actual" >&2
    exit 1
fi

rm -rf "$source_dir" "$build_dir"
tar -xJf "$tarball" -C "$work"
mkdir -p "$build_dir"

configure_args=(
    --prefix="$prefix"
    --enable-static --disable-shared
    # Nothing from the build machine gets linked in: the result is
    # self-contained and cannot acquire a dependency we did not choose.
    --disable-autodetect
    --disable-doc --disable-debug
    --disable-programs --enable-ffmpeg --enable-ffprobe
)

# x86 assembly needs nasm; without it FFmpeg still builds, just slower. Decoding
# a song to PCM is not where this app spends its time, so a missing nasm is a
# warning rather than a stop.
if [ "$arch" = "x86_64" ]; then
    if command -v nasm >/dev/null; then
        :
    else
        echo "nasm not found; building x86_64 without assembly optimisations"
        echo "(brew install nasm to get them)"
        configure_args+=(--disable-x86asm)
    fi
    if [ "$(uname -m)" != "x86_64" ]; then
        echo "cross-compiling x86_64 on $(uname -m) ..."
        configure_args+=(
            --arch=x86_64 --enable-cross-compile
            --cc="clang -arch x86_64" --host-cc=clang
        )
    fi
else
    configure_args+=(--arch=arm64 --cc="clang -arch arm64")
fi

( cd "$build_dir" && "$source_dir/configure" "${configure_args[@]}" )
( cd "$build_dir" && make -j"$(sysctl -n hw.ncpu)" && make install )

# --- prove the two things this script exists to guarantee -----------------
for name in ffmpeg ffprobe; do
    binary="$prefix/bin/$name"
    [ -x "$binary" ] || { echo "$name was not built" >&2; exit 1; }
done
if "$prefix/bin/ffmpeg" -hide_banner -version | grep -q -- '--enable-gpl'; then
    echo "built FFmpeg reports --enable-gpl, which must never ship" >&2
    exit 1
fi
for name in ffmpeg ffprobe; do
    if otool -L "$prefix/bin/$name" | tail -n +2 |
        grep -vE '^\s+(/usr/lib/|/System/Library/)' | grep -q .; then
        echo "$name links against non-system libraries:" >&2
        otool -L "$prefix/bin/$name" | tail -n +2 |
            grep -vE '^\s+(/usr/lib/|/System/Library/)' >&2
        exit 1
    fi
done

cat > "$prefix/BUILD_INFO.txt" <<INFO
FFmpeg LGPL static build (macOS $arch)
source: https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz
tarball sha256: $FFMPEG_SHA256
configure: ${configure_args[*]}
version: $("$prefix/bin/ffmpeg" -hide_banner -version | head -1)
INFO

echo
echo "ffmpeg=$prefix/bin/ffmpeg"
echo "license=LGPL (no --enable-gpl)"
echo "linkage=system libraries only"

#!/usr/bin/env bash
# Package a frozen macOS runtime for download, mirroring
# package_windows_runtime.ps1: stage the sidecar tree, compress it, and write a
# manifest the installer/app reads.
#
#   tools/package_macos_runtime.sh --arch arm64 --version 0.0.9 \
#       --ffmpeg /path/to/lgpl-ffmpeg/bin
#
# Produces dist/macos/runtime-macos-<arch>-<version>.7z and the matching .json.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arch=""
version=""
ffmpeg_dir=""
while [ $# -gt 0 ]; do
    case "$1" in
        --arch) arch="${2:-}"; shift 2 ;;
        --version) version="${2:-}"; shift 2 ;;
        --ffmpeg) ffmpeg_dir="${2:-}"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[ -n "$arch" ] || { echo "--arch arm64|x86_64 is required" >&2; exit 2; }
[ -n "$version" ] || { echo "--version is required" >&2; exit 2; }
# Deliberately not guessed. `brew install ffmpeg` is a GPL build, and shipping
# it would put this project under the GPL's corresponding-source obligation --
# see THIRD_PARTY_NOTICES.md, which closed that question with "LGPL only".
[ -n "$ffmpeg_dir" ] || {
    echo "--ffmpeg <dir> is required (an LGPL build; Homebrew's is GPL)" >&2
    exit 2
}

runtime_source="$repo_root/build/standalone-runtime-macos-$arch-dist/htdemucs-worker"
[ -x "$runtime_source/htdemucs-worker" ] || {
    echo "frozen runtime not found: $runtime_source" >&2
    echo "run tools/build_standalone_runtime_macos.sh first" >&2
    exit 1
}

seven_zip="$(command -v 7zz || command -v 7z || true)"
[ -n "$seven_zip" ] || { echo "7-Zip not found. brew install sevenzip" >&2; exit 1; }

# --- FFmpeg checks -------------------------------------------------------
# Both of these have bitten this project's Windows packaging before, and the
# macOS versions are easier to get wrong: the default Homebrew build is GPL,
# and it links against dylibs that do not travel with the binary.
for name in ffmpeg ffprobe; do
    [ -x "$ffmpeg_dir/$name" ] || { echo "missing $ffmpeg_dir/$name" >&2; exit 1; }
done
if "$ffmpeg_dir/ffmpeg" -hide_banner -version 2>/dev/null | grep -q -- '--enable-gpl'; then
    echo "refusing to package a GPL FFmpeg build: $ffmpeg_dir/ffmpeg" >&2
    echo "the release must use an LGPL build (no --enable-gpl)" >&2
    exit 1
fi
for name in ffmpeg ffprobe; do
    if otool -L "$ffmpeg_dir/$name" | tail -n +2 |
        grep -vE '^\s+(/usr/lib/|/System/Library/)' | grep -q .; then
        echo "$name links against non-system libraries, so it will not run on" >&2
        echo "another Mac. Use a static LGPL build:" >&2
        otool -L "$ffmpeg_dir/$name" | tail -n +2 |
            grep -vE '^\s+(/usr/lib/|/System/Library/)' >&2
        exit 1
    fi
done

# --- stage ---------------------------------------------------------------
stage_root="$repo_root/build/macos-runtime-$arch"
dist_root="$repo_root/dist/macos"
runtime_root="$stage_root/Resources/sidecar/Runtime"
case "$stage_root" in
    "$repo_root"/build/*) ;;
    *) echo "refusing to replace staging outside the build tree" >&2; exit 1 ;;
esac
rm -rf "$stage_root"
mkdir -p "$runtime_root/ffmpeg/bin" "$dist_root"
# -R preserves the ad-hoc signatures PyInstaller wrote; every Mach-O in here
# needs one or Apple Silicon refuses to load it.
cp -R "$runtime_source" "$runtime_root/htdemucs-worker"
cp "$ffmpeg_dir/ffmpeg" "$ffmpeg_dir/ffprobe" "$runtime_root/ffmpeg/bin/"

archive_name="runtime-macos-$arch-$version.7z"
archive="$dist_root/$archive_name"
rm -f "$archive" "$archive.partial"
# Non-solid for the same reason the Windows archives are: an installer that
# extracts entry by entry re-decompresses a solid block for every file.
( cd "$stage_root" && "$seven_zip" a -t7z -m0=LZMA2 -mx=9 -ms=off -mmt=on -y -bso0 -bsp0 \
    "$archive.partial" . >/dev/null )
mv "$archive.partial" "$archive"

archive_bytes="$(stat -f %z "$archive")"
archive_sha="$(shasum -a 256 "$archive" | cut -d' ' -f1)"
ffmpeg_sha="$(shasum -a 256 "$runtime_root/ffmpeg/bin/ffmpeg" | cut -d' ' -f1)"

python3 - "$dist_root/runtime-macos-$arch-$version.json" "$arch" "$version" \
    "$archive_name" "$archive_bytes" "$archive_sha" "$ffmpeg_sha" \
    "$runtime_source/runtime-manifest.json" <<'PY'
import json, sys
out, arch, version, name, size, sha, ffmpeg_sha, worker_manifest = sys.argv[1:9]
json.dump({
    "schema_version": 1,
    "flavor": "mps" if arch == "arm64" else "cpu",
    "platform": "macos",
    "architecture": arch,
    "version": version,
    "archives": [{"archive": name, "role": "runtime",
                  "bytes": int(size), "sha256": sha}],
    "total_bytes": int(size),
    "extract_root": "Resources/sidecar",
    "worker_manifest": json.load(open(worker_manifest)),
    "ffmpeg_sha256": ffmpeg_sha,
}, open(out, "w"), indent=4)
PY

echo "runtime_archive=$archive"
echo "bytes=$archive_bytes"
echo "sha256=$archive_sha"
echo "runtime_manifest=$dist_root/runtime-macos-$arch-$version.json"

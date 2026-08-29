#!/usr/bin/env bash
# Build HTDemucs GPU FX on macOS as a universal binary (Intel + Apple Silicon).
#
#   tools/build_macos.sh            # standalone app only (default)
#   tools/build_macos.sh --plugin   # also build the VST3
#
# Requirements: Xcode Command Line Tools (clang, macOS SDK) and CMake 3.22+.
# The RoFormer models additionally need a Python environment — see README.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build/macos"
standalone_only=ON
for arg in "$@"; do
    case "$arg" in
        --plugin) standalone_only=OFF ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

command -v cmake >/dev/null || {
    echo "cmake not found. Install it (e.g. brew install cmake) and retry." >&2
    exit 1
}
xcode-select -p >/dev/null 2>&1 || {
    echo "Xcode Command Line Tools missing. Run: xcode-select --install" >&2
    exit 1
}

# Patch the vendored JUCE exactly like the Windows build does.
if [ -f "$repo_root/patches/juce-8.0.13-htfx.patch" ]; then
    juce_dir="$repo_root/third_party/JUCE"
    if ! grep -q "createHTFXMMEAudioIODeviceType()" \
        "$juce_dir/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h" 2>/dev/null; then
        echo "applying JUCE patch..."
        git -C "$juce_dir" apply "$repo_root/patches/juce-8.0.13-htfx.patch"
    fi
fi

echo "configuring (universal: arm64 + x86_64)..."
cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    -DHTFX_BUILD_PLUGIN=ON \
    -DHTFX_STANDALONE_ONLY=$standalone_only \
    -DHTFX_PORTABLE_STANDALONE_SETTINGS=OFF

echo "building..."
cmake --build "$build_dir" --config Release \
    --target HTDemucsGpuFX_Standalone -- -j"$(sysctl -n hw.ncpu)"

app="$build_dir/HTDemucsGpuFX_artefacts/Release/HTDemucs GPU FX.app"
if [ -d "$app" ]; then
    echo
    echo "built: $app"
    binary="$app/Contents/MacOS/HTDemucs GPU FX"
    [ -f "$binary" ] && lipo -info "$binary" || true
    echo
    echo "Run it with the repository as the working directory so the RoFormer"
    echo "manifest and worker resolve, e.g.:"
    echo "    cd \"$repo_root\" && \"$binary\""
else
    echo "build finished but the .app was not found under $build_dir" >&2
    exit 1
fi

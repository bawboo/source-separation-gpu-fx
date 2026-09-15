#!/usr/bin/env bash
# Build Music SSP FX on macOS as a universal binary (Intel + Apple Silicon).
#
#   tools/build_macos.sh                   # standalone app only (default)
#   tools/build_macos.sh --plugin          # also build the VST3
#   tools/build_macos.sh --bundle-runtime  # self-contained, ad-hoc signed .app
#   tools/build_macos.sh --bundle-runtime --runtime-arch x86_64
#                                          # ... carrying the Intel runtime
#
# The binary is always universal. --runtime-arch picks which architecture's
# frozen worker travels inside it, and defaults to this machine's. Building
# the Intel bundle on an Apple Silicon Mac needs it: without it the build
# quietly stages the arm64 runtime into an app meant for Intel, and the result
# looks right until someone presses Separate on a real Intel Mac.
#
# Requirements: Xcode Command Line Tools (clang, macOS SDK) and CMake 3.22+.
# The RoFormer models additionally need a Python environment — see README.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_root/build/macos"
standalone_only=ON
bundle_runtime=OFF
runtime_arch="$(uname -m)"
while [ $# -gt 0 ]; do
    case "$1" in
        --plugin) standalone_only=OFF ;;
        # Stage the sidecar resources and a frozen runtime into the .app, then
        # ad-hoc sign it, so the bundle runs on a machine that has none of this
        # repository.
        --bundle-runtime) bundle_runtime=ON ;;
        --runtime-arch)
            runtime_arch="${2:-}"
            case "$runtime_arch" in
                arm64|x86_64) ;;
                *) echo "--runtime-arch must be arm64 or x86_64" >&2; exit 2 ;;
            esac
            shift
            ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

command -v cmake >/dev/null || {
    echo "cmake not found. Install it (e.g. brew install cmake) and retry." >&2
    exit 1
}
xcode-select -p >/dev/null 2>&1 || {
    echo "Xcode Command Line Tools missing. Run: xcode-select --install" >&2
    exit 1
}

# Checked here, before anything is built or deleted. Staging starts by
# removing the existing sidecar, so discovering the missing runtime at that
# point left the previous -- working -- bundle as rubble that still looks like
# an app: right path, universal binary, no Runtime and a broken seal. A failed
# run should cost you the run, not what you already had.
staged_runtime="$repo_root/build/macos-runtime-$runtime_arch/Resources/sidecar/Runtime"
if [ "$bundle_runtime" = ON ] && [ ! -d "$staged_runtime" ]; then
    echo "no staged runtime for $runtime_arch, so --bundle-runtime cannot" >&2
    echo "do what it was asked. Build and stage it first:" >&2
    echo "    tools/build_standalone_runtime_macos.sh --python <$runtime_arch python>" >&2
    echo "    tools/package_macos_runtime.sh --arch $runtime_arch --version dev --ffmpeg <dir>" >&2
    exit 1
fi

# Patch the vendored JUCE exactly like the Windows build does.
if [ -f "$repo_root/patches/juce-8.0.13-htfx.patch" ]; then
    juce_dir="$repo_root/third_party/JUCE"
    if ! grep -q "createHTFXMMEAudioIODeviceType()" \
        "$juce_dir/modules/juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h" 2>/dev/null; then
        echo "applying JUCE patch..."
        git -C "$juce_dir" apply "$repo_root/patches/juce-8.0.13-htfx.patch"
    fi
fi

# The minimum OS the app claims has to be the minimum the bundle can keep, and
# that differs by which runtime travels inside it. Measured across every Mach-O
# in each bundle: the Intel one bottoms out at 12.0, while the Apple Silicon one
# cannot go below 14.0 because that is what torch's arm64 wheels are stamped
# with. Declaring 12.0 for both would let the app launch on a Monterey M1 and
# then die inside the worker on the first separation -- which reads as "the
# separation is broken", not "this Mac is too old". Better to be refused at
# launch, by a message that names the reason.
deployment_target=12.0
[ "$runtime_arch" = arm64 ] && deployment_target=14.0

echo "configuring (universal: arm64 + x86_64, minimum macOS $deployment_target)..."
cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
    -DHTFX_BUILD_PLUGIN=ON \
    -DHTFX_STANDALONE_ONLY=$standalone_only \
    -DHTFX_PORTABLE_STANDALONE_SETTINGS=OFF

echo "building..."
cmake --build "$build_dir" --config Release \
    --target HTDemucsGpuFX_Standalone -- -j"$(sysctl -n hw.ncpu)"

app="$build_dir/HTDemucsGpuFX_artefacts/Release/Standalone/Music SSP FX.app"
if [ ! -d "$app" ]; then
    echo "build finished but the .app was not found under $build_dir" >&2
    exit 1
fi
echo
echo "built: $app"
binary="$app/Contents/MacOS/Music SSP FX"
[ -f "$binary" ] && lipo -info "$binary" || true

if [ "$bundle_runtime" = ON ]; then
    # Contents/Resources/sidecar is already one of the roots the app searches
    # (it looks beside the executable and one level up), so a bundle staged
    # here needs no code change and no environment variables.
    sidecar="$app/Contents/Resources/sidecar"
    echo
    echo "staging the sidecar into the bundle..."
    rm -rf "$sidecar"
    mkdir -p "$sidecar/models"
    for tree in "worker:worker" "src/htdemucs_gpu_fx:src/htdemucs_gpu_fx" \
                "third_party/demucs/demucs:demucs_repo/demucs"; do
        src="$repo_root/${tree%%:*}"
        dst="$sidecar/${tree##*:}"
        mkdir -p "$dst"
        ( cd "$src" && tar --exclude='.git' --exclude='__pycache__' \
            --exclude='*.pyc' -cf - . ) | tar -xf - -C "$dst"
    done
    # Both manifests are required: without roformer-manifest.json the mode list
    # degrades to HTDemucs 4/6-stem only.
    for manifest in model-manifest.json roformer-manifest.json roformer-catalog.json; do
        cp "$repo_root/assets/models/$manifest" "$sidecar/models/"
    done
    cp "$repo_root"/assets/models/*.yaml "$sidecar/models/" 2>/dev/null || true

    # Its absence was fatal before the sidecar was touched, so by here it
    # exists -- unless something removed it while cmake was running, which is
    # worth failing on rather than silently shipping an app that cannot
    # separate.
    [ -d "$staged_runtime" ] || {
        echo "the staged $runtime_arch runtime disappeared during the build" >&2
        exit 1
    }
    echo "staging the $runtime_arch runtime into the bundle..."
    cp -R "$staged_runtime" "$sidecar/Runtime"
    # The whole point of the flag is that this can now disagree with the build
    # machine, so say what actually went in rather than what was asked for.
    worker="$sidecar/Runtime/htdemucs-worker/htdemucs-worker"
    [ -f "$worker" ] && file -b "$worker" | head -n 1

    # Apple Silicon refuses to execute any Mach-O without a signature. The
    # frozen runtime's binaries are already ad-hoc signed by PyInstaller; this
    # re-signs the app itself, which adding files to it invalidated. Developer
    # ID + notarization is a separate, later step and needs every nested binary
    # signed with the same identity.
    echo "ad-hoc signing the bundle..."
    codesign --force --sign - --timestamp=none "$app"
    codesign --verify --verbose=2 "$app" || true
fi

echo
echo "Run it with the repository as the working directory so the RoFormer"
echo "manifest and worker resolve, e.g.:"
echo "    cd \"$repo_root\" && \"$binary\""

#!/bin/bash
set -u

portable_root="$(cd "$(dirname "$0")" && pwd)"
app="$portable_root/HTDemucs GPU FX.app"
sidecar="$app/Contents/Resources/sidecar"
worker="$sidecar/Runtime/htdemucs-worker/htdemucs-worker"
ffmpeg="$sidecar/Runtime/ffmpeg/bin/ffmpeg"
smoke="$portable_root/Tools/htdemucs_worker_registry_smoke"
models="$sidecar/models"
result="$portable_root/verification-result.txt"
log="$portable_root/verification-details.log"
architecture_file="$portable_root/package-architecture.txt"
[[ -f "$architecture_file" ]] || { echo "Missing package-architecture.txt"; exit 1; }
architecture="$(/usr/bin/tr -d '[:space:]' < "$architecture_file")"
backend="cpu"
[[ "$architecture" == "arm64" ]] && backend="mps"

exec > >(tee "$log") 2>&1
echo "HTDemucs GPU FX portable verification"
echo "Package: $architecture"
echo "Host: $(uname -m), macOS $(sw_vers -productVersion)"
rm -f "$result"

fail() {
    echo "FAIL: $1" | tee "$result"
    echo "Details: $log"
    read -r -p "Press Return to close..." _
    exit 1
}

[[ -x "$worker" ]] || fail "Bundled worker is missing"
[[ -x "$ffmpeg" ]] || fail "Bundled FFmpeg is missing"
[[ -x "$smoke" ]] || fail "Verification tool is missing"
[[ -d "$models" ]] || fail "Models are missing"

"$worker" --help | grep -q -- '--models-dir' || fail "Worker startup check failed"
"$ffmpeg" -version >/dev/null 2>&1 || fail "FFmpeg startup check failed"

echo "Running the full 8-case model/segment matrix on $backend."
echo "Each case loads and warms a full model; Intel verification can take a long time."
matrix_cases=(
    "htdemucs 2.0"
    "htdemucs 3.0"
    "htdemucs 4.0"
    "htdemucs 5.0"
    "htdemucs 7.8"
    "htdemucs_ft 2.0"
    "htdemucs_6s 3.0"
    "hdemucs_mmi 4.0"
)
completed_cases=0
for matrix_case in "${matrix_cases[@]}"; do
    read -r model segment <<< "$matrix_case"
    echo "Case $((completed_cases + 1))/${#matrix_cases[@]}: $model, ${segment}s"
    "$smoke" \
        --worker-executable "$worker" \
        --models "$models" \
        --model "$model" \
        --segment "$segment" \
        --backend "$backend" || fail "HTDemucs matrix failed at $model ${segment}s"
    completed_cases=$((completed_cases + 1))
done

clean_home="$(mktemp -d "${TMPDIR:-/tmp}/htfx-verify.XXXXXX")"
app_binary="$app/Contents/MacOS/HTDemucs GPU FX"
HOME="$clean_home" TMPDIR="$clean_home" HTFX_REQUIRE_BUNDLED_SIDECAR=1 \
    HTFX_USE_FAKE_WORKER=1 \
    "$app_binary" >/dev/null 2>&1 &
app_pid=$!
sleep 10
if ! kill -0 "$app_pid" 2>/dev/null; then
    rm -rf "$clean_home"
    fail "Standalone GUI exited during the 10-second launch check"
fi
kill "$app_pid" 2>/dev/null || true
wait "$app_pid" 2>/dev/null || true
rm -rf "$clean_home"

echo "PASS: worker, FFmpeg, 8-case HTDemucs matrix, and Standalone GUI" | tee "$result"
echo "Result saved to: $result"
read -r -p "Press Return to close..." _

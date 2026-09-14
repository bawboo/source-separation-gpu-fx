#!/usr/bin/env bash
# Wrap the built .app in a DMG that someone else can open, drag and run.
#
#   tools/package_macos_dmg.sh --arch x86_64 --version 0.0.10
#
# Produces dist/macos/MusicSSPFX-<version>-<arch>.dmg containing the app and a
# shortcut to /Applications, which is the layout every Mac user already knows.
#
# It does NOT make the app pass Gatekeeper. That needs a paid Developer ID and
# notarization; without them the first launch on someone else's Mac is refused
# until they allow it by hand, and the DMG carries a README telling them how.
# Hiding that step would only move the confusion to their screen.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arch=""
version=""
app=""
while [ $# -gt 0 ]; do
    case "$1" in
        --arch) arch="${2:-}"; shift 2 ;;
        --version) version="${2:-}"; shift 2 ;;
        --app) app="${2:-}"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[ -n "$arch" ] || { echo "--arch arm64|x86_64 is required" >&2; exit 2; }
[ -n "$version" ] || { echo "--version is required" >&2; exit 2; }
[ -n "$app" ] ||
    app="$repo_root/build/macos/HTDemucsGpuFX_artefacts/Release/Standalone/Music SSP FX.app"
[ -d "$app" ] || { echo ".app not found: $app" >&2; exit 1; }

# Both architectures build to the same path, so the only thing that says which
# one is in here is the binary itself. Ask it rather than trusting the flag --
# shipping an arm64 runtime in a DMG labelled Intel is the exact mistake
# --runtime-arch exists to prevent, and it would not be visible until a user
# pressed Separate.
worker="$app/Contents/Resources/sidecar/Runtime/htdemucs-worker/htdemucs-worker"
[ -x "$worker" ] || { echo "the app carries no frozen runtime: $worker" >&2; exit 1; }
worker_arch="$(lipo -archs "$worker" 2>/dev/null || file -b "$worker")"
case "$worker_arch" in
    *"$arch"*) ;;
    *)
        echo "this .app carries a $worker_arch runtime, not $arch." >&2
        echo "rebuild with: tools/build_macos.sh --bundle-runtime --runtime-arch $arch" >&2
        exit 1
        ;;
esac
codesign --verify --deep --strict "$app" ||
    { echo "the .app's signature does not verify; re-sign before packaging" >&2; exit 1; }

dist_root="$repo_root/dist/macos"
stage="$repo_root/build/dmg-$arch"
dmg="$dist_root/MusicSSPFX-$version-$arch.dmg"
case "$stage" in
    "$repo_root"/build/*) ;;
    *) echo "refusing to replace staging outside the build tree" >&2; exit 1 ;;
esac
rm -rf "$stage"
mkdir -p "$stage" "$dist_root"
# -R keeps the ad-hoc signatures; a plain cp would invalidate every Mach-O.
cp -R "$app" "$stage/"
ln -s /Applications "$stage/Applications"

arch_label="Apple Silicon"
standard_estimate="1 分鐘"
high_estimate="45 分鐘"
# Measured: an M1 is ready in about six seconds, an Intel build under Rosetta
# took 58 on its first run and five on every one after. Telling an Apple
# Silicon user to expect two minutes of silence would have them waiting for
# something that is not coming, and mentioning Rosetta in a file headed
# "Apple Silicon 版" reads as a warning they cannot act on.
first_run_note="程式要先把模型載進記憶體，可能幾十秒沒有動靜。"
if [ "$arch" = x86_64 ]; then
    arch_label="Intel"
    standard_estimate="3 分鐘"
    high_estimate="50 分鐘"
    first_run_note="第一次可能一到兩分鐘完全沒有動靜：程式要先把模型載進記憶體，
  而且如果你是在 Apple Silicon 的 Mac 上跑這個 Intel 版，系統還要先
  翻譯一次程式碼。"
fi
# Written for someone who has never seen this project. Every paragraph here is
# something that was measured or walked into during the port, and each one is a
# thing that looks like a defect from the outside: Gatekeeper refusing an
# unsigned app, a silent first launch, and a High quality button that is not
# broken, just slow. The instructions deliberately do not mention a Separate
# button -- the general panel has none, the export buttons start the run.
cat > "$stage/請先讀我.txt" <<TXT
Music SSP FX $version（$arch_label 版）

安裝
  把「Music SSP FX」拖到旁邊的 Applications 資料夾。

第一次開啟
  直接雙擊會被 macOS 擋下來，說「無法打開，因為無法驗證開發者」。
  這是因為這個 App 沒有付費的 Apple 開發者簽章，不是因為它有問題。

  請這樣開：
    1. 打開「系統設定」→「隱私權與安全性」
    2. 捲到最下面，會看到「已阻止 Music SSP FX」
    3. 按「仍要打開」，再按一次「打開」

  只要做這一次，之後雙擊就能開。
  （macOS 14 以前的版本：在「應用程式」裡對它按右鍵，選「打開」。）

怎麼分離一首歌
    1. 按藍色的「匯入音訊 / 影片」，選一個音樂檔或影片檔
    2. 下面那排保持「一般」就好
    3. 按「僅匯出人聲」或「僅匯出伴奏」，選要存到哪裡

  按下去之後就會開始跑，進度列會動。沒有「分離」按鈕，匯出就是開始。

  「高品質」那顆可以按，但這台電腦跑它要 $high_estimate左右（一般是
  $standard_estimate）。想試的話請留足時間。

第一次按下去會等比較久，那不是當掉
  $first_run_note請不要強制結束，第二次之後就快了。

需要網路
  第一次分離會自動下載模型檔（幾百 MB），下載完就不用再下載。
TXT

rm -f "$dmg"
hdiutil create -volname "Music SSP FX $version" -srcfolder "$stage" \
    -ov -format UDZO "$dmg" >/dev/null

echo "dmg=$dmg"
echo "bytes=$(stat -f %z "$dmg")"
echo "sha256=$(shasum -a 256 "$dmg" | cut -d' ' -f1)"
echo "runtime_arch=$worker_arch"

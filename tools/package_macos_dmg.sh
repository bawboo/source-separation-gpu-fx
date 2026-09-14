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
[ "$arch" = x86_64 ] && arch_label="Intel"
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

第一次按下「分離」會很久
  Intel 版在 Apple Silicon 上執行時，第一次要花一到兩分鐘才會開始，
  畫面上不會有動靜。那是系統在翻譯程式碼，不是當掉了，請不要強制結束。
  第二次之後就正常。

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

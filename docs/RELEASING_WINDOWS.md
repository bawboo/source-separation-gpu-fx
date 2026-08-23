# Windows release workflow

This project uses one GitHub repository. Source code lives in Git; compiled
runtime and installer files live on the release associated with the same tag.

Examples below use `OWNER/REPOSITORY` and version `0.1.0`. Replace both before
running commands.

## 1. Create and publish the source repository

Create an empty public repository on GitHub. Do not add a README, licence or
`.gitignore` on the website because these files already exist locally.

If Codex initialized the repository and your normal Windows account reports
`dubious ownership` or cannot see `origin`, trust only this resolved project
directory, then verify the remote:

```powershell
$repoPath = (Resolve-Path '.').Path.Replace('\', '/')
git config --global --add safe.directory $repoPath
git remote -v
```

```powershell
git init
git branch -M main
git add .
git status
git commit -m "Initial public source release"
git remote add origin https://github.com/OWNER/REPOSITORY.git
git push -u origin main
```

Clone checks must include both pinned submodules:

```powershell
git clone --recurse-submodules https://github.com/OWNER/REPOSITORY.git clean-checkout
```

## 2. Verify local release assets

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\verify_windows_web_packages.ps1 `
  -Version 0.1.0
powershell -ExecutionPolicy Bypass -File .\tools\check_public_repo.ps1 `
  -Version 0.1.0
```

Expected runtime assets in `dist/windows-web/`:

- `runtime-win-x64-cpu-0.1.0.zip`
- `runtime-win-x64-cuda-core-0.1.0.zip`
- `runtime-win-x64-cuda-libraries-0.1.0.zip`
- `runtime-win-x64-cpu-0.1.0.json`
- `runtime-win-x64-cuda-0.1.0.json`
- `release-manifest.json`
- `SHA256SUMS.txt`

Do not upload the obsolete `runtime-win-x64-cuda-0.1.0.zip`; it exceeds
GitHub's per-asset limit and is not referenced by the installer.

## 3. Tag the source commit

```powershell
git tag -a v0.1.0 -m "HTDemucs GPU FX v0.1.0"
git push origin v0.1.0
```

## 4. Create a draft release and upload runtimes

Using GitHub CLI keeps the large uploads resumable from a terminal and avoids
accidentally putting binaries into Git history.

```powershell
gh auth login
git ls-remote --exit-code --tags origin refs/tags/v0.1.0

gh release create v0.1.0 --draft `
  --title "HTDemucs GPU FX v0.1.0" `
  --notes-file .\docs\release-notes-v0.1.0.md

gh release upload v0.1.0 `
  .\dist\windows-web\runtime-win-x64-cpu-0.1.0.zip `
  .\dist\windows-web\runtime-win-x64-cuda-core-0.1.0.zip `
  .\dist\windows-web\runtime-win-x64-cuda-libraries-0.1.0.zip `
  .\dist\windows-web\runtime-win-x64-cpu-0.1.0.json `
  .\dist\windows-web\runtime-win-x64-cuda-0.1.0.json `
  .\dist\windows-web\release-manifest.json
```

## 5. Compile the final web installer

The URL must use the exact tag, not `latest`, so an old installer can never
silently download a newer incompatible runtime.

The standard Inno Setup install does not ship a Traditional Chinese `.isl`
file. The current build uses Inno's built-in English wizard language while
retaining the project's Traditional Chinese GPU/CPU and error messages. A full
Traditional Chinese wizard must vendor its reviewed `.isl` file in this repo.

```powershell
$releaseBaseUrl = 'https://github.com/OWNER/REPOSITORY/releases/download/v0.1.0'
powershell -ExecutionPolicy Bypass -File .\tools\build_windows_web_installer.ps1 `
  -ReleaseBaseUrl $releaseBaseUrl `
  -Version 0.1.0
```

The result is:

`dist/windows-web/installer/HTDemucs_GPU_FX_Setup_x64.exe`

Regenerate checksums so the final Setup is included, then upload both files to
the same draft release:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\check_public_repo.ps1 `
  -Version 0.1.0
gh release upload v0.1.0 `
  .\dist\windows-web\installer\HTDemucs_GPU_FX_Setup_x64.exe `
  .\dist\windows-web\SHA256SUMS.txt
```

If code signing is available, sign the EXE before the upload and regenerate
`SHA256SUMS.txt`. Never upload a private signing key or certificate password.

## 6. Test and publish

Download the release assets on a clean Windows Sandbox or VM and test both CPU
and CUDA choices. Confirm that Setup downloads the official default model,
launches the application and that uninstall offers to preserve or delete user
data.

Inspect the draft:

```powershell
gh release view v0.1.0 --json assets,isDraft,name,url
```

Then publish it:

```powershell
gh release edit v0.1.0 --draft=false --latest
```

On the GitHub website the equivalent flow is: repository **Releases** →
**Draft a new release** → select/create tag → upload assets → **Save draft**.
After Setup has been compiled and tested, edit the draft and choose
**Publish release**.

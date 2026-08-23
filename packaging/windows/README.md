# Windows Web Installer

The Windows installer is intentionally split into four primary release artifacts:

- `HTDemucs_GPU_FX_Setup_x64.exe`: hardware detection, base application and
  installer UI only.
- `runtime-win-x64-cpu-<version>.zip`: CPU-only PyTorch worker and FFmpeg.
- `runtime-win-x64-cuda-core-<version>.zip`: most of the CUDA PyTorch worker and FFmpeg.
- `runtime-win-x64-cuda-libraries-<version>.zip`: the three largest CUDA libraries.

The CUDA runtime is split because GitHub requires each Release asset to remain
below 2 GiB. Both CUDA archives preserve their relative paths and are extracted
into the same application directory.

The setup probes DXGI and the NVIDIA CUDA Driver API before showing the runtime
choice. A usable CUDA device enables the CPU/CUDA page with CUDA selected by
default. Otherwise the page is skipped and only the CPU runtime is downloaded.

The default `htdemucs` weight is never embedded in the setup or runtime ZIP. It
is downloaded during setup from Meta's `dl.fbaipublicfiles.com` endpoint and
verified against its pinned byte count and SHA-256. Other models remain
on-demand resources described by `assets/models/model-manifest.json`.

## Build order

1. Run `tools\build_windows_installed.cmd`.
2. Build both workers with `tools\build_standalone_runtime.ps1 -Flavor cuda`
   and `-Flavor cpu` using the appropriate pinned Python/PyTorch environment.
3. Run `tools\package_windows_runtime.ps1` for `cpu` and `cuda`.
4. Run `tools\package_windows_installer_payload.ps1`.
5. Run `tools\verify_windows_web_packages.ps1`.
6. Upload the CPU ZIP and both CUDA ZIP files to the intended GitHub Release.
7. Run `tools\build_windows_web_installer.ps1 -ReleaseBaseUrl <release URL>`
   on a machine with Inno Setup 6.7 or newer installed. The script depends on
   the compiler's verified `download` and `extractarchive` support.

Do not publish a setup compiled against placeholder URLs. Sign the two runtime
archives' release manifest, the setup executable, the application executable
and both worker executables before a public release.

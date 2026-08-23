# HTDemucs GPU FX v0.1.0

First Windows preview release.

## Installation

Download and run `HTDemucs_GPU_FX_Setup_x64.exe`. Setup detects whether a usable
CUDA GPU is present. CUDA is selected by default when available; otherwise the
CPU runtime is installed automatically.

The installer downloads one CPU runtime asset or both CUDA runtime assets and
verifies their SHA-256 values. It also downloads the default `htdemucs` model
directly from Meta's official host.

## Notes

- Windows x64, Windows 10 22H2 or newer.
- CUDA build: PyTorch 2.1.2 + CUDA 12.1.
- CPU build: CPU-only PyTorch runtime.
- This is a preview. Full-model realtime mode has substantial latency.

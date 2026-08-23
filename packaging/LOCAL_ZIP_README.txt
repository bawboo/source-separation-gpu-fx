HTDemucs GPU FX - private local test package
=============================================

This archive contains:
- Standalone/HTDemucs GPU FX.exe for use without a DAW.
- VST3/HTDemucs GPU FX.vst3 for copying to a VST3 plug-in folder.

Both versions include the Python worker, dependencies, and model files. They do
not include Python, PyTorch, CUDA, or FFmpeg executables.

Before launching, make a CUDA-enabled Python available as python.exe on PATH or
set HTFX_PYTHON to its executable path. For video import/export, make
ffmpeg.exe available on PATH or set HTFX_FFMPEG to its executable path.

The application and plug-in are Windows x64 local test builds. Use a 44,100 Hz
audio device or DAW project.

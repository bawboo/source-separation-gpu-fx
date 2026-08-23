HTDemucs GPU FX v0.1 - local test package
==========================================

Requirements
------------
- Windows x64
- 64-bit Python containing PyTorch. CUDA plus a compatible NVIDIA driver is
  recommended; CPU separation is supported but much slower.
- DAW project sample rate: 44,100 Hz
- Route stereo audio through the plug-in in a DAW at 44,100 Hz.

Modes and latency
-----------------
New instances start in Record mode and report zero latency. Start the DAW
transport, press the plug-in's red Record button, stop recording, then press
Separate. The preview player uses the stem sliders and Bypass recalls the
original recording. Realtime mode (Ultra high latency) is still available; its
default 7.8-second window reports 366,030 samples (about 8.30 seconds).

Media import and export
-----------------------
The General panel imports one audio/video file and provides Export Vocals only
and Export Accompany only. Each button asks for a destination and suggests
<source>_vocals.wav or <source>_accompany.wav. Quick export always uses the
default htdemucs model; accompaniment is the original-level sum of drums, bass,
and other. The first export separates when needed and the second reuses that
result. Both are 44,100 Hz stereo 32-bit float WAV files.

Open Advanced panel to use the controls described below.

In Record mode, Import audio/video decodes a file to 44,100 Hz stereo before
separation. Video requires an external FFmpeg executable. After separation,
Export opens one window with checkboxes for individual stems, an Export all
stems button, and Export mix. Stem files are 32-bit float WAVs at the original
Demucs output level and ignore the sliders. Mix WAVs use the current stem gains,
Output Trim, and Bypass. For imported video, Export mix can instead copy the
original video stream into an MP4 and replace its audio with the interface mix.

No FFmpeg binary is bundled. Set HTFX_FFMPEG or make ffmpeg.exe available on
PATH.

Window controls
---------------
Full screen toggles the Standalone window (host restrictions may apply to a
VST3 editor). Scale UI enables proportional host resizing and a draggable
bottom-right corner. All controls scale together at a fixed aspect ratio.

Advanced options
----------------
The section starts collapsed. It offers 2, 3, 4, 5, and 7.8-second windows;
htdemucs, htdemucs_ft, htdemucs_6s, and hdemucs_mmi; and Auto, NVIDIA GPU, or
CPU compute. Auto chooses CUDA when available and otherwise falls back to CPU.

Runtime paths
-------------
The VST3 bundle contains the worker, Demucs Python source, einops, julius, and
all seven official weight files needed by the four selectable models under
Contents/Resources/sidecar. htdemucs (955717e8) remains the default. Set
HTFX_PYTHON to the Python executable before launching the DAW. HTFX_GPU_WORKER
and HTFX_MODELS_DIR are optional diagnostic overrides. HTFX_FFMPEG selects the
external media tool.

Licensing notice
----------------
This is a local engineering/test package, not a redistribution-ready public
release. Before distributing it, review the JUCE licence, Demucs source/model
licences, PyTorch/CUDA runtime terms, and all third-party notices. The detailed
inventory and copied licence texts are in the Licenses folder. In particular,
the JUCE licence route and pretrained Demucs weight rights remain unresolved.

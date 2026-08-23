HTDemucs GPU FX Portable Standalone v0.1
========================================

Run "HTDemucs GPU FX.exe" directly. No VST installation is required.

First launch
------------
1. Open Audio/MIDI Settings from the application's Options button.
2. Select a stereo input and output device.
3. Set the device sample rate to exactly 44,100 Hz. If only 48,000 Hz is
   offered, choose "Windows Audio (Exclusive Mode)" as the audio device type;
   this exposed 44,100 Hz on the validated test machine.
4. JUCE enables "Mute audio input" by default to prevent a feedback loop. Only
   disable it after using headphones or otherwise making the routing safe.
5. The app starts on the General panel. Import audio/video, then use Export
   Vocals only or Export Accompany only. The app selects the default htdemucs
   model, separates when needed, and asks where to save the WAV file.

General panel
-------------
- Suggested names are <source>_vocals.wav and <source>_accompany.wav.
- Vocals is the original-level htdemucs vocals stem. Accompany is the
  original-level sum of drums, bass, and other.
- Both outputs are stereo 44,100 Hz, 32-bit float WAV files. Video imports also
  produce audio-only WAV files from these two quick-export buttons.
- A completed htdemucs separation is reused, so exporting the second result
  does not run the model again.
- Open Advanced panel for recording, preview controls, realtime mode, stem
  sliders, full export options, model selection, full screen, and UI scaling.

Import and export files
-----------------------
- Import audio/video loads an existing file instead of recording. It is decoded
  to 44,100 Hz stereo before separation.
- After separation, Export opens a window where you can check individual stems,
  export all stems, or export the current interface mix.
- Individual stems are 32-bit float WAV files at the original Demucs output
  level; stem sliders and Output Trim do not alter them.
- Mix WAV files use the current stem sliders, Output Trim, and Bypass state.
- If the input was a video, Export mix can create an MP4 that copies the
  original picture and replaces its audio with the current mix.
- FFmpeg is included, so audio/video import and video export work without a
  separate FFmpeg installation.

Window size
-----------
Full screen toggles the Standalone window. Scale UI enables a draggable corner
at the bottom right; resizing remains proportional and scales the whole UI.

Portable files
--------------
- Resources/sidecar contains the worker, Demucs source, dependencies, and all
  official weights needed by htdemucs, htdemucs_ft, htdemucs_6s, and
  hdemucs_mmi. htdemucs (955717e8) remains the default model.
- PortableData stores this application's audio-device and plug-in settings.
- Resources/sidecar/Runtime contains the complete worker, Python runtime,
  PyTorch CUDA/CPU runtime, and FFmpeg. Do not move individual files out of
  this folder.
- Licenses contains the dependency inventory and copied third-party notices.

Advanced options
----------------
- The Advanced options section starts collapsed.
- Inference windows: 2, 3, 4, 5, or 7.8 seconds.
- Compute: Auto, NVIDIA GPU, or CPU. Auto selects CUDA when available and falls
  back to CPU. CPU separation is supported but can be much slower.
- Realtime mode (Ultra high latency) keeps the older continuous streaming path.

Audio device types
------------------
The Audio/MIDI Settings window includes Windows Audio (shared), Windows Audio
(Exclusive Mode), Windows Audio (Low Latency Mode), DirectSound, and a native
MME (WinMM) backend for older interfaces. MME is a real WinMM PCM stream, not a
renamed DirectSound option.

Requirements and latency
------------------------
- Windows x64
- A compatible NVIDIA graphics driver is recommended for GPU separation.
  Python, PyTorch, the CUDA toolkit, and FFmpeg do not need to be installed.
  CPU-only execution remains available, but it can be much slower.
- 44,100 Hz stereo audio I/O
- Record mode reports zero plug-in latency because separation happens after
  recording. The default 7.8-second Realtime mode reports an 8.30-second
  pipeline and is not intended for live monitoring.

This local engineering build is not redistribution-ready. Review JUCE,
Demucs/model, PyTorch/CUDA, FFmpeg, and third-party licences before distributing
it. The JUCE licence route and pretrained Demucs weight rights are explicit
release blockers; see Licenses/THIRD_PARTY_NOTICES.md.

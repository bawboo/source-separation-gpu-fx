HTDemucs GPU FX Portable Standalone for macOS
==============================================

Open "HTDemucs GPU FX.app" directly. No Python, PyTorch, FFmpeg, Homebrew,
CUDA toolkit, or VST installation is required.

Apple Silicon uses Apple Metal Performance Shaders (MPS) by default. Intel
Macs use CPU inference. Record mode, realtime mode, four/six-stem models,
audio/video import, individual original-level stem export, interface-mix WAV,
video-with-replaced-audio MP4 export, full screen, and proportional UI scaling
match the Windows portable build.

First launch and Gatekeeper
---------------------------
This local build is ad-hoc signed unless the builder supplied an Apple
Developer ID. After downloading and extracting the ZIP, remove macOS quarantine
from the whole extracted folder before verification or first launch. In
Terminal, type `xattr -dr com.apple.quarantine ` (including the final space),
drag the extracted "HTDemucs GPU FX Portable ..." folder into Terminal, then
press Return. This is required because quarantine can block the bundled worker
and verifier, and App Translocation can prevent PortableData beside the app from
being writable. If macOS still blocks the app, Control-click it, choose Open,
then choose Open once more. A public release should use Developer ID signing,
Apple notarization, and stapling.

Verification
------------
After removing quarantine as described above, double-click "Verify HTDemucs GPU
FX.command". It checks the bundled worker and FFmpeg, performs a real
eight-case inference matrix covering all four models and all five segment
choices (MPS on Apple Silicon, CPU on Intel), and keeps the GUI open for a
ten-second launch check. Intel verification can take a long time because every
case loads and warms a full model. The result is written to
verification-result.txt beside the app.

Portable files
--------------
- The app bundle contains the Python/PyTorch worker, FFmpeg, Demucs sources,
  dependencies, and all weights for htdemucs, htdemucs_ft, htdemucs_6s, and
  hdemucs_mmi.
- PortableData beside the app stores audio-device and application settings.
  It remains outside the signed app bundle so changing settings does not break
  the signature.
- Licenses contains dependency notices. Keep the entire folder together.

Audio and media
---------------
Use Audio/MIDI Settings to choose CoreAudio input/output at exactly 44,100 Hz.
Import accepts the same audio/video formats as the Windows build. FFmpeg is
bundled for decode and video export. Individual stems are 32-bit float WAV at
the original Demucs output level; mix export applies the current stem sliders,
Output Trim, and Bypass state.

This engineering build is not notarized and is not declared redistribution-
ready. Review the notices and model/JUCE distribution rights before publishing.

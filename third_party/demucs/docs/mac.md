# macOS support for Demucs

Install [uv](https://docs.astral.sh/uv/getting-started/installation/), for instance
with Homebrew (`brew install uv`). uv takes care of installing Python for you, there
is nothing else to install. Then, anytime you want to use demucs, just run

```bash
uvx demucs PATH_TO_AUDIO_FILE_1
```

On Apple Silicon, the GPU is used automatically through Metal (MPS). If this gives
you trouble, you can force CPU separation with `-d cpu`.

**Note for Intel (non Apple Silicon) Macs**: PyTorch stopped supporting them after
version 2.2, which requires Python at most 3.12, so use `uvx --python 3.12 demucs`.

Optionally, install [ffmpeg](https://ffmpeg.org/) (`brew install ffmpeg`): it is
required for flac output and for reading the more exotic audio formats (wav, flac,
mp3, ogg, aac, etc. are decoded natively).

# Linux support for Demucs

Install [uv](https://docs.astral.sh/uv/getting-started/installation/), for instance
with `curl -LsSf https://astral.sh/uv/install.sh | sh`, or from your distribution
packages. uv takes care of installing Python for you, there is nothing else to
install. Then, anytime you want to use demucs, just run

```bash
uvx demucs PATH_TO_AUDIO_FILE_1
```

On Linux, the default PyTorch package comes with CUDA support, so if you have an
NVIDIA GPU with recent drivers, it will be used automatically. Otherwise, separation
runs on CPU (you can force it with `-d cpu`).

Optionally, install [ffmpeg](https://ffmpeg.org/) (e.g. `sudo apt-get install ffmpeg`):
it is required for flac output and for reading the more exotic audio formats (wav,
flac, mp3, ogg, aac, etc. are decoded natively).

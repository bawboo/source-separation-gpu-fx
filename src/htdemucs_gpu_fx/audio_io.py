from __future__ import annotations

import hashlib
import math
import wave
from pathlib import Path

import torch
from torch import Tensor


def generate_probe_audio(n_samples: int, sample_rate: int, seed: int) -> Tensor:
    """Create deterministic stereo material with tones, chirps, noise and impulses."""

    if n_samples <= 0:
        raise ValueError("n_samples must be positive")
    generator = torch.Generator(device="cpu")
    generator.manual_seed(seed)
    t = torch.arange(n_samples, dtype=torch.float32) / float(sample_rate)
    duration = max(n_samples / float(sample_rate), 1e-6)
    chirp_phase = 2.0 * math.pi * (55.0 * t + 0.5 * (7_500.0 - 55.0) * t.square() / duration)
    left = (
        0.12 * torch.sin(2.0 * math.pi * 110.0 * t)
        + 0.07 * torch.sin(2.0 * math.pi * 997.0 * t + 0.2)
        + 0.05 * torch.sin(chirp_phase)
    )
    right = (
        0.10 * torch.sin(2.0 * math.pi * 164.81 * t + 0.4)
        + 0.08 * torch.sin(2.0 * math.pi * 2_113.0 * t)
        + 0.05 * torch.sin(chirp_phase + 0.7)
    )
    noise = torch.randn((2, n_samples), generator=generator, dtype=torch.float32) * 0.003
    audio = torch.stack((left, right), dim=0) + noise

    # Deterministic transients make off-by-one and boundary mistakes easy to spot.
    for position, polarity in (
        (0, 1.0),
        (n_samples // 7, -1.0),
        (n_samples // 3, 1.0),
        (n_samples // 2, -1.0),
        (n_samples - 1, 1.0),
    ):
        audio[0, position] += 0.35 * polarity
        audio[1, position] -= 0.27 * polarity
    return audio.clamp_(-0.95, 0.95)


def write_pcm16_wav(path: str | Path, audio: Tensor, sample_rate: int) -> None:
    if audio.ndim != 2:
        raise ValueError("audio must have shape (channels, samples)")
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    pcm = (
        audio.detach()
        .to(device="cpu", dtype=torch.float32)
        .clamp(-1.0, 1.0)
        .transpose(0, 1)
        .mul(32_767.0)
        .round()
        .to(torch.int16)
        .contiguous()
    )
    with wave.open(str(destination), "wb") as wav:
        wav.setnchannels(int(audio.shape[0]))
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(pcm.numpy().tobytes())


def sha256_file(path: str | Path, chunk_size: int = 8 * 1024 * 1024) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(chunk_size), b""):
            digest.update(chunk)
    return digest.hexdigest()

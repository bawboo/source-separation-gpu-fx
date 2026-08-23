from __future__ import annotations

import json
import sys
from pathlib import Path

import torch

PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "src"))

from htdemucs_gpu_fx import (  # noqa: E402
    DEFAULT_PROCESSING_GUARD_SAMPLES,
    HTDEMUCS_SPEC,
)
from htdemucs_gpu_fx.audio_io import write_pcm16_wav  # noqa: E402


def main() -> int:
    spec = HTDEMUCS_SPEC
    output_dir = PROJECT_ROOT / "results" / "m1"
    output_dir.mkdir(parents=True, exist_ok=True)
    impulse_input_sample = spec.sample_rate
    block_sizes = (64, 128, 256, 512, 1024)
    reported_latency = spec.reported_plugin_latency_samples()
    latency_by_block = {str(block_size): reported_latency for block_size in block_sizes}
    longest_latency = max(latency_by_block.values())
    total_samples = impulse_input_sample + longest_latency + spec.sample_rate
    audio = torch.zeros(2, total_samples, dtype=torch.float32)
    audio[0, impulse_input_sample] = 0.8
    audio[1, impulse_input_sample] = -0.8
    wav_path = output_dir / "latency_impulse.wav"
    write_pcm16_wav(wav_path, audio, spec.sample_rate)

    fixture = {
        "sample_rate": spec.sample_rate,
        "input_impulse_sample": impulse_input_sample,
        "input_impulse_seconds": impulse_input_sample / spec.sample_rate,
        "model_lookahead_samples": spec.segment_samples,
        "processing_guard_samples": DEFAULT_PROCESSING_GUARD_SAMPLES,
        "latency_samples_by_host_block": latency_by_block,
        "expected_output_impulse_sample_by_host_block": {
            block_size: impulse_input_sample + latency
            for block_size, latency in latency_by_block.items()
        },
        "assumption": "transport starts on a host block boundary",
        "wav": str(wav_path.resolve()),
    }
    json_path = output_dir / "latency_fixture.json"
    json_path.write_text(json.dumps(fixture, indent=2), encoding="utf-8")
    print(json_path.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

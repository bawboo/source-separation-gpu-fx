from __future__ import annotations

import argparse
import time

import torch


def main() -> int:
    parser = argparse.ArgumentParser(description="Time one raw HTDemucs TorchScript CPU segment.")
    parser.add_argument("model")
    parser.add_argument("--seed", type=int, default=20260718)
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    started = time.perf_counter()
    archive = torch.jit.load(args.model, map_location="cpu").eval()
    print(f"load_seconds={time.perf_counter() - started:.3f}", flush=True)
    audio = torch.randn(1, 2, 343_980, dtype=torch.float32) * 0.01
    started = time.perf_counter()
    with torch.inference_mode():
        output = archive.nrb.model(audio)
    elapsed = time.perf_counter() - started
    print(f"segment_seconds={elapsed:.3f}")
    print(f"output_shape={tuple(output.shape)}")
    print(f"finite={bool(torch.isfinite(output).all())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

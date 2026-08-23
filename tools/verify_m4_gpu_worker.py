from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

import numpy as np
import torch

PROJECT_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = PROJECT_ROOT.parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "src"))

from htdemucs_gpu_fx import (  # noqa: E402
    HTDEMUCS_SPEC,
    StreamingOLAEngine,
    load_demucs_checkpoint,
    validate_eager_model,
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(args: argparse.Namespace) -> int:
    spec = HTDEMUCS_SPEC
    expected_input = args.hops * spec.input_channels * spec.hop_samples
    expected_output = (
        args.hops * len(spec.source_names) * spec.input_channels * spec.hop_samples
    )
    input_raw = np.fromfile(args.input, dtype=np.float32)
    worker_raw = np.fromfile(args.output, dtype=np.float32)
    if input_raw.size != expected_input:
        raise ValueError(f"input has {input_raw.size} floats, expected {expected_input}")
    if worker_raw.size != expected_output:
        raise ValueError(f"output has {worker_raw.size} floats, expected {expected_output}")
    inputs = input_raw.reshape(args.hops, spec.input_channels, spec.hop_samples)
    observed = worker_raw.reshape(
        args.hops, len(spec.source_names), spec.input_channels, spec.hop_samples
    )

    device = torch.device(f"cuda:{args.gpu}")
    if not torch.cuda.is_available():
        raise RuntimeError("CUDA is unavailable")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    model = load_demucs_checkpoint(
        args.checkpoint,
        WORKSPACE_ROOT / "work" / "demucs",
        WORKSPACE_ROOT / "work" / "bench_deps",
    )
    validate_eager_model(model, spec)
    model.to(device)
    engine = StreamingOLAEngine(model, device, spec, validate_finite=True)
    direct_hops: list[np.ndarray] = []
    for hop in inputs:
        tensor = torch.from_numpy(hop.copy()).to(device)
        direct_hops.append(engine.process_hop(tensor).cpu().numpy())
    direct = np.stack(direct_hops)

    error = observed.astype(np.float64) - direct.astype(np.float64)
    reference_rms = float(np.sqrt(np.mean(np.square(direct.astype(np.float64)))))
    error_rms = float(np.sqrt(np.mean(np.square(error))))
    relative_db = 20.0 * math.log10(max(error_rms, 1.0e-30) / max(reference_rms, 1.0e-30))
    peak_error = float(np.max(np.abs(error)))
    exact_values = int(np.count_nonzero(observed == direct))
    total_values = int(observed.size)
    finite = bool(np.isfinite(observed).all() and np.isfinite(direct).all())
    passed = finite and peak_error <= args.max_peak_error

    client_report: dict[str, object] = {}
    if args.client_report.is_file():
        client_report = json.loads(args.client_report.read_text(encoding="utf-8"))
    report = {
        "passed": passed,
        "device": str(device),
        "gpu_name": torch.cuda.get_device_name(device),
        "hops": args.hops,
        "hop_samples": spec.hop_samples,
        "source_order": list(spec.source_names),
        "finite": finite,
        "reference_rms": reference_rms,
        "error_rms": error_rms,
        "relative_rms_error_db": relative_db,
        "peak_error": peak_error,
        "max_peak_error": args.max_peak_error,
        "exact_values": exact_values,
        "total_values": total_values,
        "exact_fraction": exact_values / total_values,
        "input_sha256": sha256(args.input),
        "worker_output_sha256": sha256(args.output),
        "client": client_report,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if passed else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--client-report", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--hops", type=int, default=3)
    parser.add_argument("--gpu", type=int, default=0)
    parser.add_argument("--max-peak-error", type=float, default=1.0e-5)
    args = parser.parse_args()
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
from pathlib import Path

import torch


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MODELS = PROJECT_ROOT / "assets" / "models"
DEFAULT_DEMUCS = (
    PROJECT_ROOT
    / "dist"
    / "HTDemucs GPU FX Portable"
    / "Resources"
    / "sidecar"
    / "demucs_repo"
)
DEFAULT_DEPS = (
    PROJECT_ROOT
    / "dist"
    / "HTDemucs GPU FX Portable"
    / "Resources"
    / "sidecar"
    / "deps"
)
SEGMENTS = (2.0, 3.0, 4.0, 5.0, 7.8)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--models", type=Path, default=DEFAULT_MODELS)
    parser.add_argument("--demucs", type=Path, default=DEFAULT_DEMUCS)
    parser.add_argument("--deps", type=Path, default=DEFAULT_DEPS)
    parser.add_argument("--device", default="cuda:0")
    parser.add_argument(
        "--model",
        action="append",
        choices=("htdemucs", "htdemucs_ft", "htdemucs_6s", "hdemucs_mmi"),
    )
    parser.add_argument("--metadata-only", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    manifest_path = args.models / "model-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for name, expected in manifest["sha256"].items():
        path = args.models / name
        actual = sha256(path)
        if actual != expected:
            raise RuntimeError(f"checksum mismatch for {name}: {actual} != {expected}")

    sys.path.insert(0, str(args.deps))
    sys.path.insert(0, str(args.demucs))
    from demucs.apply import apply_model
    from demucs.pretrained import get_model

    selected = args.model or list(manifest["models"])
    device = torch.device(args.device)
    if device.type == "cuda" and not torch.cuda.is_available():
        raise RuntimeError("CUDA was requested but torch.cuda.is_available() is false")

    report: dict[str, object] = {
        "device": str(device),
        "models_root": str(args.models.resolve()),
        "segments_seconds": list(SEGMENTS),
        "models": {},
        "passed": False,
    }
    for model_name in selected:
        load_started = time.perf_counter()
        model = get_model(model_name, repo=args.models).eval()
        sources = [str(source) for source in model.sources]
        expected_sources = manifest["models"][model_name]["sources"]
        if sources != expected_sources:
            raise RuntimeError(
                f"{model_name} source order {sources} != {expected_sources}"
            )
        if int(model.samplerate) != 44_100:
            raise RuntimeError(f"{model_name} sample rate is {model.samplerate}")
        model.to(device)
        model_report: dict[str, object] = {
            "sources": sources,
            "sample_rate": int(model.samplerate),
            "load_seconds": time.perf_counter() - load_started,
            "segments": {},
        }
        if not args.metadata_only:
            for segment in SEGMENTS:
                frames = round(segment * int(model.samplerate))
                audio = torch.zeros(
                    1, int(model.audio_channels), frames, dtype=torch.float32
                )
                if device.type == "cuda":
                    torch.cuda.reset_peak_memory_stats(device)
                started = time.perf_counter()
                with torch.inference_mode():
                    output = apply_model(
                        model,
                        audio,
                        shifts=0,
                        split=False,
                        segment=segment,
                        device=device,
                    )
                if device.type == "cuda":
                    torch.cuda.synchronize(device)
                expected_shape = (1, len(sources), 2, frames)
                if tuple(output.shape) != expected_shape:
                    raise RuntimeError(
                        f"{model_name} {segment}s output {tuple(output.shape)} != {expected_shape}"
                    )
                if not torch.isfinite(output).all():
                    raise RuntimeError(f"{model_name} {segment}s produced non-finite output")
                segment_report: dict[str, object] = {
                    "frames": frames,
                    "elapsed_seconds": time.perf_counter() - started,
                    "output_shape": list(output.shape),
                }
                if device.type == "cuda":
                    segment_report["cuda_max_allocated_bytes"] = int(
                        torch.cuda.max_memory_allocated(device)
                    )
                model_report["segments"][str(segment)] = segment_report
                del output, audio
        report["models"][model_name] = model_report
        model.to("cpu")
        del model
        if device.type == "cuda":
            torch.cuda.empty_cache()

    report["passed"] = True
    rendered = json.dumps(report, ensure_ascii=False, indent=2)
    print(rendered)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

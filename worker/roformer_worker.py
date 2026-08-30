"""Headless MelBand RoFormer adapter with strict WAV output validation."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Callable

import numpy as np
import soundfile as sf


def configure_utf8_stream(stream: Any) -> None:
    """Make upstream Unicode progress output safe on Windows legacy consoles."""
    reconfigure = getattr(stream, "reconfigure", None)
    if reconfigure is not None:
        reconfigure(encoding="utf-8")


def _output_contract(input_path: Path, output_path: Path) -> dict[str, Any]:
    input_info = sf.info(input_path)
    output_info = sf.info(output_path)
    samples, _ = sf.read(output_path, dtype="float32", always_2d=True)
    finite = bool(np.isfinite(samples).all())
    if output_info.samplerate != input_info.samplerate:
        raise ValueError(f"sample-rate mismatch: {output_info.samplerate} != {input_info.samplerate}")
    if output_info.frames != input_info.frames:
        raise ValueError(f"frame-count mismatch: {output_info.frames} != {input_info.frames}")
    if output_info.channels != 2:
        raise ValueError(f"expected stereo output, got {output_info.channels} channel(s)")
    if output_info.subtype != "FLOAT":
        raise ValueError(f"expected 32-bit float WAV, got {output_info.subtype}")
    if not finite:
        raise ValueError("output contains non-finite samples")
    return {
        "sample_rate": output_info.samplerate,
        "frames": output_info.frames,
        "channels": output_info.channels,
        "subtype": output_info.subtype,
        "finite": finite,
    }


def separate_file(
    input_path: str | Path,
    output_dir: str | Path,
    *,
    model_name: str,
    models_dir: str | Path,
    device: str = "auto",
    session_factory: Callable[..., Any] | None = None,
) -> dict[str, Any]:
    input_path = Path(input_path).resolve()
    output_dir = Path(output_dir).resolve()
    if not input_path.is_file():
        raise FileNotFoundError(input_path)
    if input_path.suffix.lower() != ".wav":
        raise ValueError("input must be a WAV file")
    if session_factory is None:
        from mel_band_roformer.clean_api import MelBandRoformerSession

        session_factory = MelBandRoformerSession

    output_dir.mkdir(parents=True, exist_ok=True)
    with session_factory(
        model_name=model_name,
        models_dir=Path(models_dir),
        device=device,
        backend="torch",
    ) as session:
        manifest = session.infer(
            input_path.parent,
            store_dir=output_dir,
            output_format="wav_float32",
        )

    entries = [
        entry for entry in manifest if Path(entry["input_path"]).resolve() == input_path
    ]
    if not entries:
        raise RuntimeError(f"separator produced no output for {input_path}")

    contracts = []
    for entry in entries:
        output_path = Path(entry["output_path"]).resolve()
        contract = _output_contract(input_path, output_path)
        contracts.append({**entry, **contract})

    common = contracts[0]
    return {
        "model": model_name,
        "input": str(input_path),
        "sample_rate": common["sample_rate"],
        "frames": common["frames"],
        "channels": common["channels"],
        "subtype": common["subtype"],
        "finite": all(entry["finite"] for entry in contracts),
        "outputs": contracts,
    }


def _ensure_model_cached(model_name: str, models_dir: Path, manifest_path: Path, max_cached: int) -> None:
    """Best-effort cache priming: honors the rolling cap for manifest-listed models.

    Models absent from the manifest (e.g. the legacy default) fall through to
    the upstream session's own on-demand download, unaffected by the cap.
    """
    from roformer_cache import CacheVerificationError, ensure_cached, http_downloader

    try:
        ensure_cached(
            model_name,
            cache_dir=models_dir,
            manifest_path=manifest_path,
            downloader=http_downloader,
            max_cached=max_cached,
        )
    except KeyError:
        pass
    except CacheVerificationError as exc:
        raise RuntimeError(f"RoFormer cache verification failed for {model_name}: {exc}") from exc


def _default_manifest() -> Path:
    """Locate the model manifest for both the source tree and the frozen build.

    Frozen, this module lives inside the PyInstaller bundle, so ``__file__`` no
    longer points anywhere useful; the manifest ships in the sidecar next to the
    runtime directory instead.
    """
    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parents[2] / "models" / "roformer-manifest.json"
    return Path(__file__).resolve().parents[1] / "assets" / "models" / "roformer-manifest.json"


def main() -> int:
    configure_utf8_stream(sys.stdout)
    configure_utf8_stream(sys.stderr)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--model", default="melband-roformer-kim-vocals")
    parser.add_argument("--models-dir", type=Path, required=True)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--manifest", type=Path, default=_default_manifest())
    parser.add_argument("--max-cached", type=int, default=3)
    args = parser.parse_args()

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    _ensure_model_cached(args.model, args.models_dir, args.manifest, args.max_cached)

    print(json.dumps(separate_file(
        args.input,
        args.output_dir,
        model_name=args.model,
        models_dir=args.models_dir,
        device=args.device,
    ), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

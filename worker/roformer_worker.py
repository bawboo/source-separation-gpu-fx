"""Headless MelBand RoFormer adapter with strict WAV output validation."""

from __future__ import annotations

import argparse
import io
import json
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable

import numpy as np
import soundfile as sf


def configure_utf8_stream(stream: Any) -> Any:
    """Make progress output safe to encode, and make it arrive while it matters.

    Python block-buffers stdout when it is a pipe rather than a console, so the
    separator's progress reached the host in 4 KB bursts and the bar jumped
    instead of moving. Line buffering does not help either: upstream writes its
    per-chunk estimate with a trailing carriage return, so a newline that would
    trigger a flush never arrives. Only an unbuffered stream shows it live.
    """
    try:
        unbuffered = io.TextIOWrapper(
            open(stream.fileno(), "wb", buffering=0, closefd=False),
            encoding="utf-8",
            errors="replace",
            write_through=True,
        )
    except (AttributeError, OSError, ValueError):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8")
        return stream
    return unbuffered


PROGRESS_PREFIX = "HTFX_PROGRESS "


def report(stage: str, message: str = "", fraction: float | None = None) -> None:
    """Tell the host what this run is doing right now.

    A separation spends most of its time inside one opaque upstream call, so
    without these markers the UI cannot distinguish "still working" from
    "hung". The host parses lines starting with PROGRESS_PREFIX; anything else
    on the stream stays human-readable diagnostics.
    """
    payload = {"stage": stage}
    if message:
        payload["message"] = message
    if fraction is not None:
        payload["fraction"] = max(0.0, min(1.0, float(fraction)))
    print(PROGRESS_PREFIX + json.dumps(payload, ensure_ascii=False), flush=True)


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


def _stage_input(source: Path, destination: Path) -> None:
    """Place `source` at `destination`, preferring a link over a copy.

    The input is a full-length WAV, so hard-linking avoids copying hundreds of
    megabytes per run. Links fail across volumes and on some filesystems, in
    which case a copy is the correct fallback.
    """
    try:
        destination.hardlink_to(source)
        return
    except (OSError, AttributeError, NotImplementedError):
        pass
    shutil.copy2(source, destination)


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
    report("load", model_name)
    # infer() consumes a whole folder, so hand it one holding only this file.
    # Any other WAV beside the input would otherwise be separated as well,
    # doubling the runtime and tripping an upstream crash in the estimate
    # printer once a second track starts.
    with tempfile.TemporaryDirectory(prefix="htfx-rf-") as staging:
        staged_input = Path(staging) / input_path.name
        _stage_input(input_path, staged_input)
        with session_factory(
            model_name=model_name,
            models_dir=Path(models_dir),
            device=device,
            backend="torch",
        ) as session:
            report("infer")
            manifest = session.infer(
                staging,
                store_dir=output_dir,
                output_format="wav_float32",
            )
        # Point the entries back at the caller's file so the contract check
        # below compares against it rather than the staged copy.
        staged_resolved = staged_input.resolve()
        manifest = [
            {**entry, "input_path": str(input_path)}
            if Path(entry["input_path"]).resolve() == staged_resolved
            else entry
            for entry in manifest
        ]

    entries = [
        entry for entry in manifest if Path(entry["input_path"]).resolve() == input_path
    ]
    if not entries:
        raise RuntimeError(f"separator produced no output for {input_path}")

    report("verify")
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


def _download_reporter(name: str) -> Callable[[int, int], None]:
    """Report download progress without flooding the host with one line per chunk."""
    state = {"last": -1}

    def on_progress(received: int, total: int) -> None:
        if total <= 0:
            return
        percent = int(received * 100 / total)
        if percent == state["last"]:
            return
        state["last"] = percent
        report(
            "download",
            f"{name} {received / (1 << 20):.0f}/{total / (1 << 20):.0f} MB",
            received / total,
        )

    return on_progress


def _ensure_model_cached(model_name: str, models_dir: Path, manifest_path: Path, max_cached: int) -> None:
    """Best-effort cache priming: honors the rolling cap for manifest-listed models.

    Models absent from the manifest (e.g. the legacy default) fall through to
    the upstream session's own on-demand download, unaffected by the cap.
    """
    from roformer_cache import CacheVerificationError, ensure_cached, http_downloader
    import functools

    downloader = functools.partial(
        http_downloader, on_progress=_download_reporter(model_name))
    try:
        ensure_cached(
            model_name,
            cache_dir=models_dir,
            manifest_path=manifest_path,
            downloader=downloader,
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
    # Rebind so everything downstream - including the upstream separator, which
    # imports later - writes through the unbuffered wrappers.
    sys.stdout = configure_utf8_stream(sys.stdout)
    sys.stderr = configure_utf8_stream(sys.stderr)
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
    report("prepare", args.model)
    _ensure_model_cached(args.model, args.models_dir, args.manifest, args.max_cached)

    result = separate_file(
        args.input,
        args.output_dir,
        model_name=args.model,
        models_dir=args.models_dir,
        device=args.device,
    )
    report("done")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

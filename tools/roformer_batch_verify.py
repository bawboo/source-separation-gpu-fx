"""Batch-verify MelBand RoFormer models against assets/models/roformer-manifest.json.

Audited models: download (if needed) + SHA-256 verify via worker.roformer_cache,
then separate the given fixture and validate the output contract (worker.roformer_worker).
A failure here is a real defect (outcome="fail").

Experimental (unaudited) models: same attempt, but a failure is an acceptable,
recorded outcome per LOOP_PLAN backlog semantics ("嘗試一次並記錄結果，成敗皆可") —
outcome="attempted_failed" rather than "fail". Many experimental manifest entries
have no recorded sha256 (upstream metadata was unavailable), which fails fast with
no network call.

Prints a JSON list of per-model result dicts to stdout. Exit code is 0 iff every
*audited* model in the batch reached outcome="pass" (experimental outcomes never
affect the exit code).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "worker"))

from roformer_cache import http_downloader, ensure_cached, load_manifest_entry  # noqa: E402
from roformer_worker import configure_utf8_stream, separate_file  # noqa: E402


def verify_one(
    model_id: str,
    *,
    manifest_path: Path,
    cache_dir: Path,
    fixture: Path,
    output_root: Path,
    device: str,
    max_cached: int,
) -> dict[str, Any]:
    entry = load_manifest_entry(manifest_path, model_id)
    audited = bool(entry.get("audited"))
    result: dict[str, Any] = {
        "model": model_id,
        "category": entry.get("category"),
        "audited": audited,
    }
    try:
        if not entry.get("sha256"):
            raise ValueError("manifest entry has no recorded sha256 (checkpoint integrity cannot be verified)")
        cached = ensure_cached(
            model_id,
            cache_dir=cache_dir,
            manifest_path=manifest_path,
            downloader=http_downloader,
            max_cached=max_cached,
        )
        result["cache_verified_sha256"] = cached.sha256
        result["checkpoint_size"] = cached.size
        sep = separate_file(
            fixture,
            Path(output_root) / model_id,
            model_name=model_id,
            models_dir=cache_dir,
            device=device,
        )
        result["separation"] = {
            "sample_rate": sep["sample_rate"],
            "frames": sep["frames"],
            "channels": sep["channels"],
            "subtype": sep["subtype"],
            "finite": sep["finite"],
            "num_outputs": len(sep["outputs"]),
        }
        result["outcome"] = "pass"
    except Exception as exc:  # noqa: BLE001 - deliberately broad: every failure mode must be recorded, not raised
        result["outcome"] = "fail" if audited else "attempted_failed"
        result["error"] = f"{type(exc).__name__}: {exc}"
    return result


def main() -> int:
    configure_utf8_stream(sys.stdout)
    configure_utf8_stream(sys.stderr)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--models", nargs="+", required=True, help="manifest model ids to process, in order")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=REPO_ROOT / "assets" / "models" / "roformer-manifest.json",
    )
    parser.add_argument("--cache-dir", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--max-cached", type=int, default=3)
    args = parser.parse_args()

    results = []
    for model_id in args.models:
        print(f"--- verifying {model_id} ---", file=sys.stderr)
        result = verify_one(
            model_id,
            manifest_path=args.manifest,
            cache_dir=args.cache_dir,
            fixture=args.fixture,
            output_root=args.output_root,
            device=args.device,
            max_cached=args.max_cached,
        )
        print(f"    outcome={result['outcome']}", file=sys.stderr)
        results.append(result)

    print(json.dumps(results, ensure_ascii=False, indent=2))
    ok = all(r["outcome"] == "pass" for r in results if r["audited"])
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())

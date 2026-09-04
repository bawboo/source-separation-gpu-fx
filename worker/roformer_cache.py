"""On-demand RoFormer checkpoint cache: SHA-256 verification + rolling eviction.

Downloads a checkpoint named in ``assets/models/roformer-manifest.json`` into a
per-model subdirectory of the cache, verifies it against the manifest's
recorded sha256, and evicts the least-recently-used model directories once the
cache holds more than ``max_cached`` entries (LOOP_PLAN A6: cap 3).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

DEFAULT_MAX_CACHED = 3
DEFAULT_MANIFEST_PATH = Path(__file__).resolve().parents[1] / "assets" / "models" / "roformer-manifest.json"


class CacheVerificationError(RuntimeError):
    """A downloaded (or previously cached) checkpoint failed SHA-256 verification."""


@dataclass(frozen=True)
class CachedModel:
    model_id: str
    checkpoint_path: Path
    sha256: str
    size: int


def _sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_manifest_entry(manifest_path: str | Path, model_id: str) -> dict:
    manifest = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    for entry in manifest["models"]:
        if entry["id"] == model_id:
            return entry
    raise KeyError(f"unknown RoFormer model id: {model_id}")


def _model_dir(cache_dir: Path, model_id: str) -> Path:
    return Path(cache_dir) / model_id


def evict_oldest(cache_dir: str | Path, *, keep: int) -> list[str]:
    """Delete the oldest-by-mtime model subdirectories beyond `keep`; return deleted ids."""
    cache_dir = Path(cache_dir)
    if not cache_dir.is_dir():
        return []
    entries = [p for p in cache_dir.iterdir() if p.is_dir()]
    entries.sort(key=lambda p: p.stat().st_mtime)
    deleted = []
    while len(entries) > keep:
        victim = entries.pop(0)
        shutil.rmtree(victim)
        deleted.append(victim.name)
    return deleted


def ensure_cached(
    model_id: str,
    *,
    cache_dir: str | Path,
    manifest_path: str | Path = DEFAULT_MANIFEST_PATH,
    downloader: Callable[[str, Path], None],
    max_cached: int = DEFAULT_MAX_CACHED,
) -> CachedModel:
    """Guarantee `model_id`'s checkpoint is present & SHA-256-verified under cache_dir.

    Downloads on demand via `downloader(url, destination_path)` when missing or
    corrupt; a mismatch after a fresh download raises (never keeps a bad file).
    Marks the model directory as most-recently-used and evicts the oldest
    entries beyond `max_cached` afterwards, so the cache never grows past the
    rolling cap.
    """
    cache_dir = Path(cache_dir)
    entry = load_manifest_entry(manifest_path, model_id)
    expected_sha256 = entry.get("sha256")
    if not expected_sha256:
        raise ValueError(f"manifest entry {model_id} has no recorded sha256")

    model_dir = _model_dir(cache_dir, model_id)
    checkpoint_path = model_dir / entry["checkpoint"]

    actual_sha256 = _sha256_of(checkpoint_path) if checkpoint_path.is_file() else None
    if actual_sha256 != expected_sha256:
        if checkpoint_path.is_file():
            checkpoint_path.unlink()
        model_dir.mkdir(parents=True, exist_ok=True)
        downloader(entry["url"], checkpoint_path)
        actual_sha256 = _sha256_of(checkpoint_path) if checkpoint_path.is_file() else None
        if actual_sha256 != expected_sha256:
            if checkpoint_path.is_file():
                checkpoint_path.unlink()
            raise CacheVerificationError(
                f"SHA-256 mismatch for {model_id}: expected {expected_sha256}, "
                f"got {actual_sha256!r}"
            )

    # Prime the config too. The upstream loader fetches a missing config from
    # its own registry, and several of those URLs are dead (404) even though
    # the checkpoint is fine - a fresh install then fails at load time with
    # "could not obtain its config". The manifest carries a working config_url
    # and its SHA-256, and the loader skips its own download when the file is
    # already beside the checkpoint.
    config_name = entry.get("config")
    config_url = entry.get("config_url")
    if config_name and config_url:
        config_path = model_dir / config_name
        expected_config_sha = entry.get("config_sha256")
        expected_size = entry.get("config_size")

        def config_ok() -> bool:
            if not config_path.is_file():
                return False
            if expected_config_sha:
                return _sha256_of(config_path) == expected_config_sha
            return expected_size is None or config_path.stat().st_size == expected_size

        if not config_ok():
            if config_path.is_file():
                config_path.unlink()
            downloader(config_url, config_path)
            if not config_ok():
                if config_path.is_file():
                    config_path.unlink()
                raise CacheVerificationError(
                    f"config verification failed for {model_id} ({config_name})"
                )

    os.utime(model_dir, None)
    evict_oldest(cache_dir, keep=max_cached)

    return CachedModel(
        model_id=model_id,
        checkpoint_path=checkpoint_path,
        sha256=actual_sha256,
        size=checkpoint_path.stat().st_size,
    )


def http_downloader(
    url: str,
    destination: Path,
    on_progress: Callable[[int, int], None] | None = None,
    *,
    attempts: int = 6,
) -> None:
    """Stream `url` to `destination`; real network path used outside tests.

    A checkpoint is up to 1.7 GB, and a single connection reset used to fail
    the whole separation. Interrupted transfers are resumed with a Range
    request from the bytes already on disk, and retried with backoff; only
    after every attempt fails does the error propagate.

    `on_progress(received, total)` is called as bytes arrive so the caller can
    show a real percentage instead of an unbounded wait.
    """
    import time

    import requests

    destination.parent.mkdir(parents=True, exist_ok=True)
    tmp = destination.with_suffix(destination.suffix + ".part")
    last_error: Exception | None = None
    for attempt in range(attempts):
        received = tmp.stat().st_size if tmp.is_file() else 0
        headers = {"Range": f"bytes={received}-"} if received else {}
        try:
            with requests.get(url, stream=True, timeout=60, headers=headers) as response:
                if received and response.status_code == 200:
                    # Server ignored the range: start over rather than append.
                    received = 0
                    tmp.unlink(missing_ok=True)
                elif received and response.status_code == 416:
                    # Already complete on disk.
                    break
                else:
                    response.raise_for_status()
                content_range = response.headers.get("Content-Range", "")
                if content_range and "/" in content_range:
                    total = int(content_range.rsplit("/", 1)[1])
                else:
                    total = received + int(response.headers.get("Content-Length") or 0)
                with open(tmp, "ab" if received else "wb") as handle:
                    for chunk in response.iter_content(chunk_size=1 << 20):
                        if not chunk:
                            continue
                        handle.write(chunk)
                        received += len(chunk)
                        if on_progress is not None:
                            on_progress(received, total)
            break
        except (requests.exceptions.RequestException, OSError) as exc:
            last_error = exc
            if attempt + 1 < attempts:
                time.sleep(min(30, 2 ** attempt))
    else:
        raise RuntimeError(
            f"download failed after {attempts} attempts: {url}"
        ) from last_error
    tmp.replace(destination)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True, help="manifest model id")
    parser.add_argument("--cache-dir", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST_PATH)
    parser.add_argument("--max-cached", type=int, default=DEFAULT_MAX_CACHED)
    args = parser.parse_args()

    result = ensure_cached(
        args.model,
        cache_dir=args.cache_dir,
        manifest_path=args.manifest,
        downloader=http_downloader,
        max_cached=args.max_cached,
    )
    cached_ids = sorted(p.name for p in Path(args.cache_dir).iterdir() if p.is_dir())
    print(json.dumps({
        "model": result.model_id,
        "checkpoint_path": str(result.checkpoint_path),
        "sha256": result.sha256,
        "size": result.size,
        "cache_dir_entries": cached_ids,
        "max_cached": args.max_cached,
    }, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

import hashlib
import importlib.util
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "worker" / "roformer_cache.py"


def load_cache_module():
    spec = importlib.util.spec_from_file_location("roformer_cache", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    # dataclass() inspects sys.modules[cls.__module__] for annotation lookup.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


GOOD_CONTENT = b"real-checkpoint-bytes"
GOOD_SHA256 = hashlib.sha256(GOOD_CONTENT).hexdigest()
BAD_CONTENT = b"corrupted-checkpoint-bytes"


def write_manifest(path: Path, model_ids: list[str]) -> None:
    models = [
        {
            "id": model_id,
            "checkpoint": f"{model_id}.ckpt",
            "url": f"https://example.invalid/{model_id}.ckpt",
            "sha256": GOOD_SHA256,
            "size": len(GOOD_CONTENT),
        }
        for model_id in model_ids
    ]
    path.write_text(json.dumps({"models": models}), encoding="utf-8")


def seed_cached_model(cache_dir: Path, model_id: str, *, age_seconds: float, content: bytes = GOOD_CONTENT) -> None:
    """Pre-populate a valid cache entry with an explicit (backdated) mtime."""
    model_dir = cache_dir / model_id
    model_dir.mkdir(parents=True)
    (model_dir / f"{model_id}.ckpt").write_bytes(content)
    stamp = time.time() - age_seconds
    os.utime(model_dir, (stamp, stamp))


class GoodDownloader:
    def __init__(self):
        self.calls = []

    def __call__(self, url, destination):
        self.calls.append((url, destination))
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(GOOD_CONTENT)


class BadDownloader:
    def __init__(self):
        self.calls = 0

    def __call__(self, url, destination):
        self.calls += 1
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(BAD_CONTENT)


class RoformerCacheTest(unittest.TestCase):
    def setUp(self):
        self.module = load_cache_module()
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.cache_dir = self.root / "cache"
        self.manifest_path = self.root / "manifest.json"

    def test_downloads_when_missing_and_verifies_sha256(self):
        write_manifest(self.manifest_path, ["model-a"])
        downloader = GoodDownloader()

        result = self.module.ensure_cached(
            "model-a",
            cache_dir=self.cache_dir,
            manifest_path=self.manifest_path,
            downloader=downloader,
        )

        self.assertEqual(len(downloader.calls), 1)
        self.assertTrue(result.checkpoint_path.is_file())
        self.assertEqual(result.sha256, GOOD_SHA256)
        self.assertEqual(result.checkpoint_path.read_bytes(), GOOD_CONTENT)

    def test_skips_download_when_already_verified(self):
        write_manifest(self.manifest_path, ["model-a"])
        seed_cached_model(self.cache_dir, "model-a", age_seconds=10)
        downloader = GoodDownloader()

        self.module.ensure_cached(
            "model-a",
            cache_dir=self.cache_dir,
            manifest_path=self.manifest_path,
            downloader=downloader,
        )

        self.assertEqual(downloader.calls, [])

    def test_redownloads_when_cached_file_is_corrupt(self):
        write_manifest(self.manifest_path, ["model-a"])
        seed_cached_model(self.cache_dir, "model-a", age_seconds=10, content=BAD_CONTENT)
        downloader = GoodDownloader()

        result = self.module.ensure_cached(
            "model-a",
            cache_dir=self.cache_dir,
            manifest_path=self.manifest_path,
            downloader=downloader,
        )

        self.assertEqual(len(downloader.calls), 1)
        self.assertEqual(result.sha256, GOOD_SHA256)

    def test_raises_and_deletes_when_redownload_still_mismatches(self):
        write_manifest(self.manifest_path, ["model-a"])
        downloader = BadDownloader()

        with self.assertRaises(self.module.CacheVerificationError):
            self.module.ensure_cached(
                "model-a",
                cache_dir=self.cache_dir,
                manifest_path=self.manifest_path,
                downloader=downloader,
            )

        self.assertFalse((self.cache_dir / "model-a" / "model-a.ckpt").exists())

    def test_unknown_model_id_raises_key_error(self):
        write_manifest(self.manifest_path, ["model-a"])

        with self.assertRaises(KeyError):
            self.module.ensure_cached(
                "no-such-model",
                cache_dir=self.cache_dir,
                manifest_path=self.manifest_path,
                downloader=GoodDownloader(),
            )

    def test_rolling_cache_evicts_oldest_beyond_cap(self):
        write_manifest(self.manifest_path, ["model-a", "model-b", "model-c"])
        seed_cached_model(self.cache_dir, "model-a", age_seconds=300)
        seed_cached_model(self.cache_dir, "model-b", age_seconds=200)
        seed_cached_model(self.cache_dir, "model-c", age_seconds=100)
        write_manifest(self.manifest_path, ["model-a", "model-b", "model-c", "model-d"])
        downloader = GoodDownloader()

        self.module.ensure_cached(
            "model-d",
            cache_dir=self.cache_dir,
            manifest_path=self.manifest_path,
            downloader=downloader,
            max_cached=3,
        )

        self.assertEqual(len(downloader.calls), 1)
        remaining = sorted(p.name for p in self.cache_dir.iterdir() if p.is_dir())
        self.assertEqual(remaining, ["model-b", "model-c", "model-d"])

    def test_touching_an_existing_model_protects_it_from_eviction(self):
        write_manifest(self.manifest_path, ["model-a", "model-b", "model-c"])
        seed_cached_model(self.cache_dir, "model-a", age_seconds=300)
        seed_cached_model(self.cache_dir, "model-b", age_seconds=200)
        seed_cached_model(self.cache_dir, "model-c", age_seconds=100)
        downloader = GoodDownloader()

        # Re-request the oldest entry (model-a): no re-download, but it should
        # be marked most-recently-used so a later eviction spares it.
        self.module.ensure_cached(
            "model-a",
            cache_dir=self.cache_dir,
            manifest_path=self.manifest_path,
            downloader=downloader,
            max_cached=3,
        )
        self.assertEqual(downloader.calls, [])

        write_manifest(self.manifest_path, ["model-a", "model-b", "model-c", "model-d"])
        self.module.ensure_cached(
            "model-d",
            cache_dir=self.cache_dir,
            manifest_path=self.manifest_path,
            downloader=downloader,
            max_cached=3,
        )

        remaining = sorted(p.name for p in self.cache_dir.iterdir() if p.is_dir())
        self.assertEqual(remaining, ["model-a", "model-c", "model-d"])

    def test_evict_oldest_direct_call_respects_keep_count(self):
        module = self.module
        now = time.time()
        for offset, name in enumerate(["m1", "m2", "m3"]):
            model_dir = self.cache_dir / name
            model_dir.mkdir(parents=True)
            stamp = now - (3 - offset) * 10
            os.utime(model_dir, (stamp, stamp))

        deleted = module.evict_oldest(self.cache_dir, keep=2)

        self.assertEqual(deleted, ["m1"])
        remaining = sorted(p.name for p in self.cache_dir.iterdir() if p.is_dir())
        self.assertEqual(remaining, ["m2", "m3"])


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import hashlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "worker"))
sys.path.insert(0, str(PROJECT_ROOT / "src"))

import gpu_ipc_worker as worker  # noqa: E402


class FakeResponse(io.BytesIO):
    status = 200

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.close()


class ModelDownloadTests(unittest.TestCase):
    def manifest(self, payload: bytes, url: str = "https://models.example/model.th"):
        return {
            "registry_version": 2,
            "download_host_allowlist": ["models.example"],
            "models": {"test": {"files": ["model.th"]}},
            "artifacts": {
                "model.th": {
                    "bytes": len(payload),
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "url": url,
                }
            },
        }

    def test_download_verifies_and_atomically_installs(self):
        payload = b"known model payload"
        with tempfile.TemporaryDirectory() as temporary:
            models = Path(temporary)
            (models / "model-manifest.json").write_text(
                json.dumps(self.manifest(payload)), encoding="utf-8"
            )
            status = models / "status.json"
            with mock.patch.object(
                worker, "urlopen", return_value=FakeResponse(payload)
            ):
                self.assertEqual(worker.install_registry_model(models, "test", status), 0)
            self.assertEqual((models / "model.th").read_bytes(), payload)
            self.assertFalse((models / "model.th.partial").exists())
            self.assertEqual(json.loads(status.read_text())["state"], "installed")

    def test_rejects_non_allowlisted_host_before_network(self):
        payload = b"payload"
        with tempfile.TemporaryDirectory() as temporary:
            models = Path(temporary)
            (models / "model-manifest.json").write_text(
                json.dumps(self.manifest(payload, "https://evil.invalid/model.th")),
                encoding="utf-8",
            )
            with mock.patch.object(worker, "urlopen") as request:
                with self.assertRaisesRegex(ValueError, "not allowlisted"):
                    worker.install_registry_model(models, "test", models / "status.json")
                request.assert_not_called()


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest import mock


PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "worker"))

import gpu_ipc_worker as worker  # noqa: E402


class DeviceResolutionTests(unittest.TestCase):
    def test_auto_prefers_nvidia_cuda_when_available(self) -> None:
        with mock.patch.object(worker.torch.cuda, "is_available", return_value=True):
            self.assertEqual(str(worker.resolve_device("auto")), "cuda:0")

    def test_auto_prefers_mps_when_cuda_is_unavailable(self) -> None:
        with (
            mock.patch.object(worker.torch.cuda, "is_available", return_value=False),
            mock.patch.object(worker.torch.backends.mps, "is_available", return_value=True),
        ):
            self.assertEqual(str(worker.resolve_device("auto")), "mps")

    def test_auto_falls_back_to_cpu_without_accelerator(self) -> None:
        with (
            mock.patch.object(worker.torch.cuda, "is_available", return_value=False),
            mock.patch.object(worker.torch.backends.mps, "is_available", return_value=False),
        ):
            self.assertEqual(str(worker.resolve_device("auto")), "cpu")

    def test_forced_cuda_fails_clearly_without_cuda(self) -> None:
        with mock.patch.object(worker.torch.cuda, "is_available", return_value=False):
            with self.assertRaisesRegex(RuntimeError, "CUDA was requested"):
                worker.resolve_device("cuda:0")

    def test_forced_mps_fails_clearly_when_unavailable(self) -> None:
        with (
            mock.patch.object(worker.torch.backends.mps, "is_available", return_value=False),
            mock.patch.object(worker.torch.backends.mps, "is_built", return_value=True),
        ):
            with self.assertRaisesRegex(RuntimeError, "MPS was requested"):
                worker.resolve_device("mps")


if __name__ == "__main__":
    unittest.main()

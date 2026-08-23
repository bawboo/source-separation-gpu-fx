from __future__ import annotations

import sys
import unittest
from pathlib import Path

import torch
from torch import nn

PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "src"))

from htdemucs_gpu_fx import StreamSpec, StreamingOLAEngine  # noqa: E402


TEST_SPEC = StreamSpec(
    sample_rate=100,
    input_channels=2,
    source_names=("a", "b", "c", "d"),
    segment_samples=16,
    hop_samples=12,
    overlap_samples=4,
)


class ScaledIdentitySeparator(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.anchor = nn.Parameter(torch.zeros((), dtype=torch.float32), requires_grad=False)
        self.register_buffer("scales", torch.tensor([0.25, 0.5, 1.0, -0.75]))

    def forward(self, audio: torch.Tensor) -> torch.Tensor:
        return audio[:, None, :, :] * self.scales[None, :, None, None]


class StreamingMathTests(unittest.TestCase):
    def make_engine(self) -> StreamingOLAEngine:
        return StreamingOLAEngine(ScaledIdentitySeparator(), "cpu", TEST_SPEC)

    def test_aligned_result_is_exact_for_stateless_separator(self) -> None:
        torch.manual_seed(7)
        audio = torch.randn(2, TEST_SPEC.hop_samples * 3 + 5) * 0.1
        actual = self.make_engine().separate_aligned(audio)
        scales = torch.tensor([0.25, 0.5, 1.0, -0.75])[:, None, None]
        expected = audio[None, :, :] * scales
        torch.testing.assert_close(actual, expected, rtol=1e-6, atol=1e-7)

    def test_first_raw_hop_contains_overlap_preroll(self) -> None:
        engine = self.make_engine()
        hop = torch.arange(2 * TEST_SPEC.hop_samples, dtype=torch.float32).reshape(2, -1)
        result = engine.process_hop(hop)
        self.assertTrue(torch.equal(result[..., : TEST_SPEC.overlap_samples], torch.zeros_like(result[..., : TEST_SPEC.overlap_samples])))
        torch.testing.assert_close(
            result[2, :, TEST_SPEC.overlap_samples :],
            hop[:, : TEST_SPEC.hop_samples - TEST_SPEC.overlap_samples],
        )

    def test_wrong_hop_shape_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "hop shape"):
            self.make_engine().process_hop(torch.zeros(2, TEST_SPEC.hop_samples - 1))

    def test_common_host_block_pdc(self) -> None:
        self.assertEqual(TEST_SPEC.block_quantized_algorithmic_latency(4), 16)
        self.assertEqual(TEST_SPEC.block_quantized_algorithmic_latency(5), 19)
        self.assertEqual(TEST_SPEC.reported_plugin_latency_samples(5), 21)


if __name__ == "__main__":
    unittest.main()

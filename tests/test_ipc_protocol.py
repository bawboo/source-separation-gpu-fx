from __future__ import annotations

import sys
import unittest
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "worker"))

import ipc_protocol as p  # noqa: E402


class IpcProtocolTests(unittest.TestCase):
    def test_production_sized_double_buffer_layout(self) -> None:
        self.assertEqual(
            p.INPUT_SLOT_BYTES, p.CHANNELS * p.MAX_FRAMES * 4
        )
        self.assertEqual(
            p.OUTPUT_SLOT_BYTES,
            p.MAX_SOURCES * p.CHANNELS * p.MAX_FRAMES * 4,
        )
        self.assertEqual(p.INPUT_SLOT_STRIDE % 64, 0)
        self.assertEqual(p.OUTPUT_SLOT_STRIDE % 64, 0)
        self.assertEqual(p.TOTAL_BYTES, 28_898_560)

    def test_header_64_bit_fields_are_aligned(self) -> None:
        for offset in (
            p.OFF_EPOCH,
            p.OFF_REQUEST_SEQUENCE,
            p.OFF_RESPONSE_SEQUENCE,
            p.OFF_HEARTBEAT_COUNTER,
            p.OFF_INPUT_CHECKSUM,
            p.OFF_OUTPUT_CHECKSUM,
        ):
            self.assertEqual(offset % 8, 0)

    def test_session_names_are_local(self) -> None:
        self.assertEqual(p.mapping_name("demo"), r"Local\demo_shm")
        self.assertEqual(p.event_name("demo", "ready"), r"Local\demo_ready")


if __name__ == "__main__":
    unittest.main()

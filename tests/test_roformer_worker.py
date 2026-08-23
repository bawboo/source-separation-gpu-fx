import importlib.util
import io
import tempfile
import unittest
from pathlib import Path

import numpy as np
import soundfile as sf


ROOT = Path(__file__).resolve().parents[1]
WORKER_PATH = ROOT / "worker" / "roformer_worker.py"


def load_worker():
    spec = importlib.util.spec_from_file_location("roformer_worker", WORKER_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FakeSession:
    def __init__(self, **kwargs):
        self.kwargs = kwargs

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        return False

    def infer(self, input_folder, *, store_dir, output_format):
        input_path = next(Path(input_folder).glob("*.wav"))
        audio, sample_rate = sf.read(input_path, dtype="float32", always_2d=True)
        output_path = Path(store_dir) / f"{input_path.stem}_vocals.wav"
        output_path.parent.mkdir(parents=True, exist_ok=True)
        sf.write(output_path, audio * 0.5, sample_rate, subtype="FLOAT")
        return [{
            "input_path": str(input_path),
            "track_id": input_path.stem,
            "output_id": "vocals",
            "output_path": str(output_path),
        }]


class RoformerWorkerTest(unittest.TestCase):
    def test_configure_utf8_stream_allows_upstream_progress_symbols(self):
        worker = load_worker()
        raw = io.BytesIO()
        stream = io.TextIOWrapper(raw, encoding="cp950")

        worker.configure_utf8_stream(stream)
        stream.write("\N{ANTICLOCKWISE DOWNWARDS AND UPWARDS OPEN CIRCLE ARROWS}")
        stream.flush()

        self.assertEqual(stream.encoding.lower().replace("-", ""), "utf8")

    def test_separate_file_returns_valid_float32_stereo_stem(self):
        worker = load_worker()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_path = root / "input.wav"
            output_dir = root / "outputs"
            audio = np.linspace(-0.5, 0.5, 9600, dtype=np.float32)
            stereo = np.column_stack((audio, -audio))
            sf.write(input_path, stereo, 48000, subtype="FLOAT")

            result = worker.separate_file(
                input_path,
                output_dir,
                model_name="test-model",
                models_dir=root / "cache",
                device="cpu",
                session_factory=FakeSession,
            )

            self.assertEqual(result["sample_rate"], 48000)
            self.assertEqual(result["frames"], 9600)
            self.assertEqual(result["channels"], 2)
            self.assertEqual(result["subtype"], "FLOAT")
            self.assertTrue(result["finite"])
            self.assertEqual(result["outputs"][0]["output_id"], "vocals")


if __name__ == "__main__":
    unittest.main()

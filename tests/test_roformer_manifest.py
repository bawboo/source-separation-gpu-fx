import importlib.util
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "assets" / "models" / "roformer-manifest.json"
VALIDATOR_PATH = ROOT / "tools" / "validate_roformer_manifest.py"


def load_validator():
    spec = importlib.util.spec_from_file_location("validate_roformer_manifest", VALIDATOR_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class RoformerManifestTest(unittest.TestCase):
    def test_pinned_catalog_has_expected_audit_split_and_download_metadata(self):
        validator = load_validator()
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))

        validator.validate_manifest(manifest)

        models = manifest["models"]
        self.assertEqual(manifest["source_revision"], "f3781bc766b3")
        self.assertEqual(len(models), 99)
        self.assertEqual(sum(model["audited"] for model in models), 57)
        self.assertEqual(sum(model["experimental"] for model in models), 42)
        self.assertEqual(len({model["id"] for model in models}), 99)
        for model in models:
            self.assertTrue(model["name"])
            self.assertTrue(model["category"])
            self.assertTrue(model["url"].startswith("https://"))
            self.assertEqual(model["experimental"], not model["audited"])
            if model["audited"]:
                self.assertRegex(model["sha256"], r"^[0-9a-f]{64}$")
                self.assertGreater(model["size"], 0)


if __name__ == "__main__":
    unittest.main()

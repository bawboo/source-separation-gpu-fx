#!/usr/bin/env python
"""Validate the generated HTDemucs RoFormer model manifest."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


SHA256 = re.compile(r"^[0-9a-f]{64}$")
REQUIRED_FIELDS = {
    "id",
    "name",
    "category",
    "checkpoint",
    "config",
    "url",
    "config_url",
    "sha256",
    "size",
    "config_sha256",
    "config_size",
    "audited",
    "experimental",
}


def validate_manifest(manifest: dict) -> None:
    if manifest.get("schema_version") != 1:
        raise ValueError("schema_version must be 1")
    if manifest.get("source_revision") != "f3781bc766b3":
        raise ValueError("manifest must use the approved 99-model upstream revision")
    models = manifest.get("models")
    if not isinstance(models, list) or len(models) != 99:
        raise ValueError("manifest must contain exactly 99 models")

    seen = set()
    for index, model in enumerate(models):
        missing = REQUIRED_FIELDS - set(model)
        if missing:
            raise ValueError(f"model {index} missing fields: {sorted(missing)}")
        slug = model["id"]
        if not isinstance(slug, str) or not slug or slug in seen:
            raise ValueError(f"model {index} has an invalid or duplicate id")
        seen.add(slug)
        for field in ("name", "category", "checkpoint", "config"):
            if not isinstance(model[field], str) or not model[field]:
                raise ValueError(f"{slug}: {field} must be a non-empty string")
        for field in ("url", "config_url"):
            if not isinstance(model[field], str) or not model[field].startswith("https://"):
                raise ValueError(f"{slug}: {field} must be an HTTPS URL")
        if not isinstance(model["audited"], bool) or not isinstance(model["experimental"], bool):
            raise ValueError(f"{slug}: audit flags must be booleans")
        if model["experimental"] == model["audited"]:
            raise ValueError(f"{slug}: experimental must be the inverse of audited")
        if model["sha256"] is not None and not SHA256.fullmatch(model["sha256"]):
            raise ValueError(f"{slug}: invalid checkpoint sha256")
        if model["size"] is not None and (
            not isinstance(model["size"], int) or model["size"] <= 0
        ):
            raise ValueError(f"{slug}: invalid checkpoint size")
        if model["audited"] and (model["sha256"] is None or model["size"] is None):
            raise ValueError(f"{slug}: audited model lacks checkpoint integrity metadata")

    audited = sum(model["audited"] for model in models)
    if audited != 57:
        raise ValueError(f"expected 57 audited models, found {audited}")
    if sum(model["experimental"] for model in models) != 42:
        raise ValueError("expected 42 experimental models")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "manifest",
        nargs="?",
        type=Path,
        default=Path("assets/models/roformer-manifest.json"),
    )
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    validate_manifest(manifest)
    audited = sum(model["audited"] for model in manifest["models"])
    print(
        f"roformer manifest: {len(manifest['models'])} models, "
        f"{audited} audited, {len(manifest['models']) - audited} experimental PASS"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

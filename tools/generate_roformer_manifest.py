#!/usr/bin/env python
"""Generate the HTDemucs RoFormer catalog from the approved upstream revision."""

from __future__ import annotations

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from urllib.request import Request, urlopen


SOURCE_COMMIT = "f3781bc766b34ce9e14d008ab15748f3ab711ec1"
SOURCE_REVISION = SOURCE_COMMIT[:12]
RAW_ROOT = (
    "https://raw.githubusercontent.com/openmirlab/melband-roformer-infer/"
    f"{SOURCE_COMMIT}/src/mel_band_roformer"
)
CHECKPOINT_FALLBACK = (
    "https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models/"
)
CONFIG_FALLBACK = (
    "https://raw.githubusercontent.com/TRvlvr/application_data/main/"
    "mdx_model_data/mdx_c_configs/"
)
STATIC_CHECKPOINT_URLS = {
    "MelBandRoformer.ckpt": (
        "https://huggingface.co/KimberleyJSN/melbandroformer/resolve/main/"
        "MelBandRoformer.ckpt"
    )
}
PACKAGED_CONFIGS = {"config_vocals_mel_band_roformer.yaml"}
LEGACY_AUDITED_CONFIG_URLS = {
    filename: (
        "https://huggingface.co/Politrees/UVR_resources/resolve/"
        "495f5b4625eb092fe1fbe336f0b67c847566e52e/Roformer_models/"
        f"{filename}"
    )
    for filename in (
        "mel_band_roformer_crowd_aufr33_viperx_sdr_8.7144_config.yaml",
        "denoise_mel_band_roformer_aufr33_sdr_27.9959_config.yaml",
        "denoise_mel_band_roformer_aufr33_aggr_sdr_27.9768_config.yaml",
    )
}


def fetch_json(relative_path: str) -> dict:
    request = Request(f"{RAW_ROOT}/{relative_path}", headers={"User-Agent": "HTDemucs-GPU-FX"})
    with urlopen(request, timeout=30) as response:
        return json.load(response)


def fetch_bytes(url: str) -> bytes:
    request = Request(url, headers={"User-Agent": "HTDemucs-GPU-FX"})
    with urlopen(request, timeout=30) as response:
        return response.read()


def generate_manifest() -> dict:
    registry = fetch_json("data/melband_models.json")["models"]
    checksums = fetch_json("data/checksums.json")["files"]
    overrides = fetch_json("data/overrides.json")
    checkpoint_urls = {**STATIC_CHECKPOINT_URLS, **overrides.get("checkpoints", {})}
    config_urls = overrides.get("configs", {})
    legacy_config_metadata = {}
    for filename, url in LEGACY_AUDITED_CONFIG_URLS.items():
        content = fetch_bytes(url)
        legacy_config_metadata[filename] = {
            "sha256": hashlib.sha256(content).hexdigest(),
            "size": len(content),
        }

    models = []
    for slug, source in registry.items():
        checkpoint = source["checkpoint"]
        config = source["config"]
        checkpoint_metadata = checksums.get(checkpoint)
        config_metadata = checksums.get(config) or legacy_config_metadata.get(config)
        packaged_config = config in PACKAGED_CONFIGS
        audited = checkpoint_metadata is not None and (
            config_metadata is not None or packaged_config
        )
        models.append(
            {
                "id": slug,
                "name": source["name"],
                "category": source["category"],
                "checkpoint": checkpoint,
                "config": config,
                "url": checkpoint_urls.get(checkpoint, f"{CHECKPOINT_FALLBACK}{checkpoint}"),
                "config_url": (
                    f"{RAW_ROOT}/configs/{config}"
                    if packaged_config
                    else LEGACY_AUDITED_CONFIG_URLS.get(
                        config, config_urls.get(config, f"{CONFIG_FALLBACK}{config}")
                    )
                ),
                "sha256": checkpoint_metadata["sha256"] if checkpoint_metadata else None,
                "size": checkpoint_metadata["size"] if checkpoint_metadata else None,
                "config_sha256": config_metadata["sha256"] if config_metadata else None,
                "config_size": config_metadata["size"] if config_metadata else None,
                "audited": audited,
                "experimental": not audited,
            }
        )

    return {
        "schema_version": 1,
        "source_repository": "https://github.com/openmirlab/melband-roformer-infer",
        "source_revision": SOURCE_REVISION,
        "source_commit": SOURCE_COMMIT,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "models": models,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("assets/models/roformer-manifest.json"),
    )
    args = parser.parse_args()
    manifest = generate_manifest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    temporary.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    temporary.replace(args.output)
    audited = sum(model["audited"] for model in manifest["models"])
    print(
        f"generated {args.output}: {len(manifest['models'])} models "
        f"({audited} audited, {len(manifest['models']) - audited} experimental)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

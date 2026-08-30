"""Collect the licence files of the exact distributions frozen into a runtime.

THIRD_PARTY_NOTICES.md requires shipping the notice of the *exact* bundled
distribution, so the files are read from the interpreter that froze the runtime
rather than from a hard-coded list of versions that silently goes stale.

Run with the same python.exe that was passed to build_standalone_runtime.ps1:

    python tools/collect_runtime_licenses.py --destination <dir>
"""

from __future__ import annotations

import argparse
import importlib.metadata as metadata
import json
import shutil
import sys
from pathlib import Path

# Everything the frozen worker can import at runtime. Missing entries are
# reported rather than skipped silently, so a packaging change cannot quietly
# drop a notice.
PACKAGES = [
    "torch",
    "numpy",
    "PyYAML",
    "tqdm",
    "einops",
    "julius",
    "soundfile",
    "librosa",
    "ml_collections",
    "beartype",
    "rotary-embedding-torch",
    "melband-roformer-infer",
    "scipy",
    "numba",
    "llvmlite",
    "soxr",
    "audioread",
    "lazy_loader",
    "msgpack",
    "decorator",
    "platformdirs",
    "pooch",
    "joblib",
    "requests",
    "urllib3",
    "certifi",
    "charset-normalizer",
    "idna",
    "packaging",
    "filelock",
    "fsspec",
    "sympy",
    "mpmath",
    "networkx",
    "typing_extensions",
    "jinja2",
    "MarkupSafe",
    "cffi",
    "pycparser",
]

LICENCE_STEMS = ("LICENSE", "LICENCE", "COPYING", "NOTICE", "AUTHORS")


def licence_files(dist: metadata.Distribution) -> list[Path]:
    found: list[Path] = []
    for entry in dist.files or []:
        name = Path(str(entry)).name
        if not name.upper().startswith(LICENCE_STEMS):
            continue
        resolved = Path(dist.locate_file(entry))
        if resolved.is_file():
            found.append(resolved)
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    args.destination.mkdir(parents=True, exist_ok=True)
    collected: list[dict[str, object]] = []
    missing: list[str] = []

    for name in PACKAGES:
        try:
            dist = metadata.distribution(name)
        except metadata.PackageNotFoundError:
            missing.append(name)
            continue
        files = licence_files(dist)
        if not files:
            missing.append(f"{name} (installed, no licence file in the wheel)")
            continue
        folder = args.destination / f"{dist.metadata['Name']}-{dist.version}"
        folder.mkdir(parents=True, exist_ok=True)
        for source in files:
            shutil.copy2(source, folder / source.name)
        collected.append({
            "name": dist.metadata["Name"],
            "version": dist.version,
            "files": [f.name for f in files],
        })

    report = {
        "python": sys.version.split()[0],
        "collected": collected,
        "missing": missing,
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")

    print(f"collected {len(collected)} licence bundles into {args.destination}")
    if missing:
        print("not found: " + ", ".join(missing))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

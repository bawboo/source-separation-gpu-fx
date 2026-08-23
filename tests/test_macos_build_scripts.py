from __future__ import annotations

import re
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]


def test_embedded_python_blocks_compile() -> None:
    script = (PROJECT_ROOT / "tools" / "macos" / "build_macos_portable.sh").read_text(
        encoding="utf-8"
    )
    blocks = re.findall(r"<<'PY'\n(.*?)\nPY", script, flags=re.DOTALL)
    assert len(blocks) >= 6
    for index, block in enumerate(blocks, start=1):
        compile(block, f"build_macos_portable.sh heredoc {index}", "exec")


def test_verifier_covers_shared_model_matrix() -> None:
    script = (PROJECT_ROOT / "tools" / "macos" / "verify_portable.command").read_text(
        encoding="utf-8"
    )
    expected_cases = {
        "htdemucs 2.0",
        "htdemucs 3.0",
        "htdemucs 4.0",
        "htdemucs 5.0",
        "htdemucs 7.8",
        "htdemucs_ft 2.0",
        "htdemucs_6s 3.0",
        "hdemucs_mmi 4.0",
    }
    assert all(f'"{case}"' in script for case in expected_cases)
    assert "${#matrix_cases[@]}" in script


def test_intel_python_commands_keep_rosetta_wrapper() -> None:
    script = (PROJECT_ROOT / "tools" / "macos" / "build_macos_portable.sh").read_text(
        encoding="utf-8"
    )
    assert "bootstrap_command=(/usr/bin/arch -x86_64" in script
    assert "python_command=(/usr/bin/arch -x86_64" in script
    assert not re.search(r'^\s*"\$python"\s', script, flags=re.MULTILINE)

"""Append per-model M001..M099 items to .loop/backlog.json from the RoFormer manifest.

Idempotent: an M-item already present (matched by its "model_id" field) is left
untouched (append-only per LOOP_PLAN — never delete or reword an existing item).
Item order follows assets/models/roformer-manifest.json's model order.
"""

from __future__ import annotations

import json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = REPO_ROOT / "assets" / "models" / "roformer-manifest.json"
BACKLOG_PATH = REPO_ROOT / ".loop" / "backlog.json"


def main() -> int:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    models = manifest["models"]
    backlog = json.loads(BACKLOG_PATH.read_text(encoding="utf-8"))

    existing_model_ids = {item["model_id"] for item in backlog if "model_id" in item}
    base_priority = max((item.get("priority", 0) for item in backlog), default=0)

    appended = []
    for index, entry in enumerate(models, start=1):
        model_id = entry["id"]
        if model_id in existing_model_ids:
            continue
        audited = bool(entry.get("audited"))
        item_id = f"M{index:03d}"
        if audited:
            title = (
                f"{item_id}: {model_id}（audited, category={entry.get('category')}）—— "
                "下載+SHA-256 驗證 → 對測試音檔分離 → 輸出規格驗證（時長不變/stereo/32-bit float/有限值）必過"
            )
        else:
            title = (
                f"{item_id}: {model_id}（experimental, category={entry.get('category')}）—— "
                "已收錄進 registry/UI 並標註 experimental；嘗試一次分離並記錄結果（成敗皆可）"
            )
        backlog.append(
            {
                "id": item_id,
                "title": title,
                "passes": False,
                "priority": base_priority + index,
                "source": "plan",
                "model_id": model_id,
                "audited": audited,
                "evidence": None,
                "check": (
                    "python tools/roformer_batch_verify.py --models "
                    f"{model_id} --cache-dir C:/CodexProjects/SourceSeparation_GPU_FX/verify/roformer-cache "
                    "--fixture C:/CodexProjects/SourceSeparation_GPU_FX/verify/fixtures/test_48k_2s.wav "
                    "--output-root C:/CodexProjects/SourceSeparation_GPU_FX/verify/output/roformer-batch"
                ),
            }
        )
        appended.append(item_id)

    if appended:
        tmp_path = BACKLOG_PATH.with_suffix(".json.tmp")
        tmp_path.write_text(json.dumps(backlog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        tmp_path.replace(BACKLOG_PATH)

    print(f"appended {len(appended)} M-items (total backlog size now {len(backlog)})")
    print("first appended:", appended[:3])
    print("last appended:", appended[-3:])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

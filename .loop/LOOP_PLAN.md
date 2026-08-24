# LOOP_PLAN — melband-roformer-integration

> Status: APPROVED (2026-08-23T10:30+08:00, user reply "直接LOOP"; inferred items all confirmed)
> Field names and status values stay in English; free-text values follow the
> user's language (繁體中文). `none` = empty field.

## 1. Goal
把 https://github.com/openmirlab/melband-roformer-infer 的全部 99 個 source-separation 模型，以「擴充現有一般面板模型選單為分類式 model browser（分類／搜尋／下載狀態／按需下載）」的 UX 整合進 HTDemucs GPU FX（Windows standalone），推論走獨立 conda env 的開發 Python worker；57 個稽核可用模型端到端必過，42 個未稽核模型收錄並標註 experimental。(stated + asked)

## 2. Completion criteria
All criteria must hold simultaneously (AND).

| # | Type | Definition | Checked by |
|---|---|---|---|
| C1 | command | `cmd /c .loop\checks\full.cmd` exits 0 —— 內含：standalone 建置成功＋`htdemucs_record_mode_smoke`＋`htdemucs_media_io_smoke`＋`htdemucs_ui_configuration_smoke` 全 PASS（不退步）＋新增 `htdemucs_roformer_smoke` PASS | full tier |
| C2 | command | backlog checker exits 0：`python -c "import json,sys; sys.exit(any(not i['passes'] for i in json.load(open('.loop/backlog.json',encoding='utf-8'))))"` —— 全部 backlog 項目 passes=true | full tier |
| C3 | command | `python .loop/check_scope.py` exits 0（範圍無違規） | every iteration |

Backlog 語意（asked）：57 個稽核模型每個一項「下載＋SHA-256 驗證→對測試音檔分離→輸出規格驗證（時長不變、stereo、32-bit float、有限值）」必過；42 個未稽核模型每個一項「收錄進 registry/UI＋標註 experimental＋嘗試一次並記錄結果（成敗皆可）」；另含架構項目（conda env、registry 匯入、worker、C++ 整合、model browser UI、下載管理器＋滾動快取、roformer smoke test、README 新章節）。
Plateau parameters: none
Combination rule: AND
Backlog: `.loop/backlog.json` seeded at wiring（架構項目 A1–A10＋模型枚舉項；per-model 項目由早期 iteration 從 repo registry 產生後 append，append-only——flip `passes` with printed evidence, never delete or reword；C2 為記帳、C1 為不可美化的指令證據，兩者 AND）。

## 3. Verification
- Cheap tier (every iteration): `cmd /c .loop\checks\cheap.cmd` —— 增量建置 standalone＋ui_configuration_smoke＋（存在後）registry 驗證 script 與本次目標模型的單項 check。預期 2–6 分鐘。(asked)
- Full tier (every 5 iterations and before declaring convergence): `cmd /c .loop\checks\full.cmd`＋C2 backlog checker。(asked)
- Working directory: `C:\CodexProjects\SourceSeparation_GPU_FX\HTDemucs_GPU_FX_Codex_Transfer_2026-08-22` · Timeout per run: 30 min
- Checker independence: deterministic commands 為基準；宣告 converged 前必須以 fresh-context verifier（Task subagent 或另起 `claude -p`）從零重跑 full tier 並比對，證據列印。(asked)

## 4. Tool permissions
Allowed (prose)：shell（建置/測試/conda/pip/ffmpeg/git）；網路僅限下載與查文件（HF 模型、PyPI/GitHub 套件、文件頁），**禁止任何上傳／push／發佈**；套件只裝進獨立 conda env `htfx-roformer`；git 僅本機 commit 於 loop branch（**絕不設 remote、絕不 push**）；不刪除、不整檔覆寫任何既有檔案（在 allow_paths 內編輯原始碼可以；唯一例外：`verify\roformer-cache\` 內模型權重檔可刪——滾動快取需要）；subagents 允許（收斂前獨立重驗）。
Everything else: disallowed → pause-and-ask (`blocked` / `blocked_permission_required`).
```json
{
  "permissions": {
    "allow_shell": true,
    "allow_network": "download_only",
    "allow_install": "conda_env_htfx-roformer_only",
    "allow_git_commit": true,
    "allow_delete": false,
    "allow_subagents": true
  },
  "scope": {
    "allow_paths": ["plugin/", "cpp/", "worker/", "tests/", "tools/", "assets/models/", "docs/", "CMakeLists.txt", "README.md", ".gitignore"],
    "deny_paths": ["third_party/", "patches/", "dist/", "build/windows-web/", "assets/models/*.th", "assets/models/*.yaml", "assets/models/model-manifest.json", "AGENTS.md", "CODEX_HANDOFF.md", "THIRD_PARTY_NOTICES.md", "docs/RELEASING_WINDOWS.md", "TRANSFER_BINARY_SHA256.txt", ".gitattributes", ".gitmodules"]
  }
}
```
Provenance — network: asked · install: asked · git: asked · delete: asked · deny_paths: asked · shell: inferred-confirmed · subagents: inferred-confirmed

## 5. Scope
- In scope (may modify): `plugin/`、`cpp/`、`worker/`、`tests/`、`tools/`、`CMakeLists.txt`、`assets/models/`（僅新增檔案，例如 roformer registry json）、`docs/`（新增檔案）、`README.md`（新增章節）、`.gitignore`（新增條目）。repo 外：`C:\CodexProjects\SourceSeparation_GPU_FX\verify\roformer-cache\`（模型滾動快取，可建可刪）、`verify\output\`（測試輸出）。
- Out of scope / do-not-touch: `third_party/`（JUCE＋demucs）、`patches/`、`dist/`、`build/windows-web/`、`assets/models` 既有檔（*.th／*.yaml／model-manifest.json）、五份移交基準文件、`TRANSFER_BINARY_SHA256.txt`、repo 外的 `verify\payload-cuda\`。(asked)
- Waived concerns: macOS/VST3 路徑不管；frozen runtime／installer 打包不在本輪（留待下一階段）；分離「品質」不設指標（只驗格式與流程正確）。(asked/inferred)
- Enforcement: worker self-check + Engine B driver post-check via `.loop/check_scope.py`

## 6. Budget
- max_iterations: 60 (asked)
- stall_threshold: 6 consecutive iterations without improvement (asked)
- max_wall_time: none（`MAX_WALL_MINUTES=0`——配合跨用量 reset 續跑）(asked)
- iteration_timeout: 60 min（`ITERATION_TIMEOUT_MINUTES=60`；2026-08-24 由 40 修訂——慢速網路下同步下載大模型兩度超時）(asked, amended)
- improvement epsilon: 任一 criterion 轉 pass 或任一 backlog 項目轉 passes=true (inferred)

## 7. Failure / stall policy
strategy-switch（最多 1 次換質性不同方法，再卡即 pause-and-ask）。(asked)
Broken verification harness (command errors twice in a row): always pause-and-ask.

## 8. Checkpointing & autonomy
- Checkpointing: git commit every iteration on branch `loop/melband-roformer`（先 `git init` 本機 repo＋main 基準 commit；絕不設 remote）；commit message `loop(iter-<N>): <status> — <summary>`；commit even on failed verification。(asked)
- Autonomy: fully autonomous until termination（無人值守；`max_iterations` 為安全網）。(stated)

## 9. Execution engine
- Engine: B — external driver script (asked; 使用者要求「五小時額度耗盡後 reset 自動續跑」)
- **Crank line**: 外部驅動器 `.loop/run_loop.sh`（Git Bash＋nohup/tmux）發動每一次 iteration，每輪以 `claude -p` 全新 context 執行；只要 driver 程序活著且 `state.json.status=="running"` 就持續；偵測到 usage limit 時睡 20 分鐘重試、不計入 iteration；kill switch：`touch .loop/STOP`、Ctrl+C／kill driver、或改 `state.json`。
- Engine B — AGENT_CMD_JSON: `["claude","-p","--permission-mode","acceptEdits","--allowedTools","Bash Edit Write Read Glob Grep Task WebFetch WebSearch"]`（headless 無互動提示；Bash 全開——護欄由 policy.json＋deny_paths＋check_scope＋git 承擔；模型用 CLI 預設） · PROMPT_MODE: arg · MAX_WALL_MINUTES: 0 · ITERATION_TIMEOUT_MINUTES: 40 · SLEEP_SECONDS: 10
  Driver 客製（本專案）：(1) python3 → anaconda `python` shim（Windows Git Bash）；(2) usage-limit 偵測——agent 輸出含 usage/rate limit 樣式且 iteration 未推進時，睡 20 分鐘後重試，不計入 failed launch。
  Launch — pilot first: `MAX_LAUNCHES=1 bash .loop/run_loop.sh`（看完一輪：record 寫入、journal append、commit 存在）→ `nohup bash .loop/run_loop.sh &`（或 tmux）walk-away · rerun to resume

## Amendments
- iter 0, 2026-08-23T11:20+08:00: §9 AGENT_CMD_JSON: claude.exe headless → codex.exe `exec --skip-git-repo-check --dangerously-bypass-approvals-and-sandbox`（原因：claude CLI 在本機未登入（"Not logged in"），headless 無法認證；codex exec 實測已認證可用。引擎仍為 B、其餘不變。使用者已於 2026-08-23 授權自行運行維護）
- iter 0, 2026-08-23T11:20+08:00: §4 追加使用者規則：絕不允許刪除 `C:\CodexProjects\SourceSeparation_GPU_FX\` 專案根目錄以外的任何檔案（已寫入 LESSONS.md SIGN）
- iter 14, 2026-08-24T22:15+08:00: §6 iteration_timeout: 40 → 60 min（連續兩次同步模型下載超時；配合 STEER 單模型批次＋15 分下載止損。操作性 ceiling 調整，未放寬任何完成標準）
- iter 16+, 2026-08-24: §1 UX 精緻化（使用者試用回饋，user-directed）：面板改為「模式優先」——先選分離模式（各模式有預設模型、可換同類替代），選定後拉桿才 enable。以 backlog B1–B4（source:user）實作，優先於剩餘 M 項。目標與完成標準未放寬；C2 涵蓋新 B 項。

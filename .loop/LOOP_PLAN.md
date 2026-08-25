# LOOP_PLAN — ui-language-and-defaults

> Status: APPROVED (2026-08-25T11:45+08:00, 使用者確認三問後「沿用配置直接開跑」)
> 沿用上一輪（melband-roformer-integration，已收斂歸檔於 .loop-archive-20260825-1141）的權限、範圍、引擎與護欄；本檔只記差異與新目標。

## 1. Goal
介面雙語化：**預設繁體中文**、可切換英文（選擇持久化）；全部 UI 文字（控件/選單/模式/狀態列/錯誤訊息）雙語，模型 ID 保持英文原名。啟動預設狀態：**預選「人聲分離」模式**（預設模型 kim-vocals），其餘設定皆有合理 default、無「未選擇」死狀態；上次選擇記憶。(stated + asked)

## 2. Completion criteria (AND)
| # | Type | Definition |
|---|---|---|
| C1 | command | `cmd //c '.loop\checks\full.cmd'` exits 0（四個 smoke 全 PASS，含更新後的雙語斷言） |
| C2 | command | backlog checker exits 0（python one-liner 同上一輪） |
| C3 | command | `python .loop/check_scope.py` exits 0 |

Backlog: L1 語言基礎架構（字串表 zh-TW/en＋設定持久化＋切換控件）→ L2 全 UI 文字雙語化（預設 zh-TW）→ L3 英文切換完整、即時生效 → L4 啟動預選「人聲分離」＋上次選擇持久化 → L5 全設定 defaults 審視（segment/compute/mode 等皆有合理預設）→ L6 ui smoke 擴充（zh 預設斷言、en 切換斷言、啟動預選斷言、持久化斷言；**既有英文字串斷言改為依語言取值——這是行為變更的必要測試更新，非放寬**）→ L7 full tier 回歸。append-only；flip with printed evidence。

## 3-5. Verification / Permissions / Scope
同上一輪（cheap 每輪＋full 每 5 輪與收斂前＋收斂前 fresh-context 獨立重驗；權限與 deny_paths 沿用 policy.json 原檔未動）。

## 6. Budget
max_iterations: 30 · stall_threshold: 5 · iteration_timeout: 60 min · max_wall_time: none · epsilon: 任一 criterion 或 backlog 項轉綠 (asked)

## 7-8. Failure / Checkpointing
同上一輪：strategy-switch 一次後 pause；驗證連錯兩次 pause。git 每輪 commit 於 branch `loop/ui-language`（origin: loop/melband-roformer @ 8223700526be4d3df5a92ed092c6b3e9f15f7664）；絕不設 remote。

## 9. Execution engine
Engine B，同上一輪 AGENT_CMD_JSON（claude.exe headless）與 driver 客製（usage-limit 睡 20 分續跑）。Crank line 同前；kill switch：touch .loop/STOP。

## Amendments
none
- iter 0, 2026-08-25T15:20+08:00: §6 iteration_timeout: 60 → 90 min（L1 觸及 CMakeLists → 每個建置循環全量重建 15–20 分鐘，60 分鐘兩度被 hang guard 終止；小批次 STEER 仍然有效。操作性 ceiling 調整）

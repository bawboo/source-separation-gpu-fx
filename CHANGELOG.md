# Changelog

本專案的版本紀錄。格式依循 [Keep a Changelog](https://keepachangelog.com/zh-TW/1.1.0/)，
版本號依循 [語意化版本](https://semver.org/lang/zh-TW/)。

## [0.0.1] - 2026-08-30

第一個標記版本。在既有的 HTDemucs 原型上加入 MelBand RoFormer 模型家族、
模式優先的操作介面、多檔批次流程與中英雙語介面。

### 新增

- **MelBand RoFormer 整合**：99 個模型（人聲／伴奏／卡拉 OK／吉他／去混響／
  去噪／群聲／氣音等 10 類），其中 57 個經逐一端到端驗證，其餘標註 experimental
- **模式優先介面**：12 種分離模式，各自帶預設模型並可換同類替代；選定模式後
  拉桿才啟用，且拉桿名稱隨模式改變
- **多檔批次流程**：多選匯入、每檔一列（勾選框／檔名／狀態）、點列切換到該檔的
  調音介面、每檔獨立記住拉桿設定、逐一推論（先完成先可預覽）、只匯出勾選的檔案
  並一次輸出到指定資料夾
- **按需下載**：選用的 RoFormer 模型自動下載並驗證 SHA-256，滾動快取上限 3 個
- **中英雙語介面**：預設繁體中文，可即時切換英文並記住選擇
- **啟動預設**：預選「人聲分離」模式，上次的模式與模型於下次啟動還原
- **macOS 建置支援**：universal binary（Intel ＋ Apple Silicon）設定與
  `tools/build_macos.sh`，說明見 `docs/MACOS_BUILD.md`
- **端到端驗收工具** `tests/goal_check.cpp`：對一首真實歌曲跑完四種分離模式的
  「匯入→分離→匯出人聲→匯出伴奏」

### 修正

- 快速匯出會強制把模型切回 `htdemucs`，與模式優先介面衝突，導致在任何 RoFormer
  模式下按匯出都等不到結果
- `isRoformerModelName()` 只比對 `melband-roformer-` 前綴，使 99 個模型中的 74 個
  （`roformer-model-*`）被誤判為 HTDemucs 模型而拒絕分離
- `SpscRing` 在編譯期值初始化約 75 MB 儲存區，造成編譯器堆積耗盡（C1060），
  並在每次啟動時白白歸零這些頁面
- 快速匯出僅支援 HTDemucs 4/6 軌結果，RoFormer 的 2 軌結果無法匯出
- RoFormer 模型未安裝時被前置檢查擋下，未讓 worker 走按需下載
- CPU 模式警告訊息未納入中文化
- 各分離模式切換時，音軌拉桿名稱停留在前一個模式（例如仍顯示鼓／貝斯）

### 效能

- 5 分鐘歌曲的分離時間：HTDemucs 4 軌 43→16 秒、RoFormer 人聲 83→63 秒

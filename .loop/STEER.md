（續跑後第一個 iteration 的一次性指令）
1. 先清理移交樹內誤建的 `verify/` 目錄（相對路徑 bug 的殘留，含部分下載 .ckpt.part——屬快取例外可刪），確認 `git status` 乾淨後再開始本輪 change-set。
2. 之後所有模型快取操作一律使用絕對路徑 C:\CodexProjects\SourceSeparation_GPU_FX\verify\roformer-cache\。
3. audited 模型批次縮小為每輪同步完成 2–4 個（下載→SHA-256→分離→驗證→記錄，全部在本輪內完成，不留背景工作）。

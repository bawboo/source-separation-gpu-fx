# Repository-specific instructions

- 若 Codex 初始化或操作此 Git repository，必須從使用者的 Windows 工作階段驗證 repository ownership、remote 與目標 tag。
- 若 Git 回報 dubious ownership，或 `origin does not appear to be a git repository` 但 remote 設定實際存在，先取得此 repository 的解析後絕對路徑，再只將該路徑加入全域 `safe.directory`；不得使用 `safe.directory '*'` 或其他萬用設定。
- 修正後重新執行原本失敗的 Git 指令，確認問題確實排除後再繼續 GitHub Release 流程。
- 執行特定 CMake target 前，先從 `CMakeLists.txt` 或產生的 `.vcxproj` 確認完整名稱；若回報 target 不存在，修正後必須重跑原驗證指令。
- 修改媒體匯入或預覽播放路徑後，必須以 48/96 kHz 來源及 48/96 kHz 播放裝置驗證時長一致，並確認 44.1 kHz 模型與匯出格式不受影響。

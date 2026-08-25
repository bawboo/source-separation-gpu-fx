（一次性指令，2026-08-25 13:25 投放）
上一輪（被 60 分鐘 ceiling 終止）已留下未 commit 的繼承工作：plugin/Localization.h/.cpp（新檔）＋ CMakeLists.txt、plugin/PluginProcessor.cpp、tests/ui_configuration_smoke.cpp 修改。本輪：
1. 先跑 cheap tier 建立繼承基準（協定 step 4 的 dirty-tree 規則）。
2. 本輪目標縮小為：讓繼承的 Localization 模組「編譯通過＋cheap tier 綠」即可（必要時先縮小接線範圍——例如只接分離模式選單），有 record 有 commit 就收。
3. L1 剩餘接線與 L2 全量字串留給後續輪次，每輪一個子里程碑。

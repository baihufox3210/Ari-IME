# 架構分析與實作計畫

基線 f673839，原工作樹乾淨。Git refs 受限，重構前已保存完整 Git checkpoint：
`/tmp/ari-ime-before-p0-20261026.bundle`；可用 `git clone <bundle> restored` 復原。

## 現有架構

C++20 / Fcitx5 InputMethodEngineV2 / libchewing。Buffer 持有每個 input context 的
live 注音、英文、chewing run、凍結 cells、句中插入 tail，以及 caret/picking 狀態。
KeyResult 分開表達 handled、commit、UI；ari_ime.cpp 負責平台呼叫及
CommonCandidateList / client preedit。Zhuyin 包裝候選、詞組及讀音；user_data
管理個人資料。WASM 共用 Buffer，但不含 Linux 平台層。

已有詞組候選、局部修正、游標移動、候選鎖定、Tab/翻頁、數字/滑鼠選字、
選字 undo、加權學習與敏感欄位停止學習。先保護這些能力，不另造平行引擎。

## P0 根因與決策

舊平台攔截貼上，讀 clipboard addon 並插入 preedit，換行被改寫；
讀不到 clipboard 時卻直接放行並保留組字。一般 Ctrl 快捷鍵也有此交接問題；
Shift+方向鍵會被當作普通組字移動。

統一 commit-before-pass-through：確認目前顯示的整段文字，保留未完成音節的
原始按鍵與句中插入尾段；不暗自選取尚未確認的高亮候選。清空組字/候選後，
讓原始按鍵交給應用程式。沒有 clipboard 讀取，也不合成或重送按鍵。
Ctrl+C/X/V/A、client undo/redo、Ctrl+Backspace 及 Shift+方向/Home/End 適用。
既有候選修正 undo 優先留在組字；Ctrl+Left/Right、明確標點、模式鍵仍由引擎處理。
modifier-only、release 保留原行為。Ctrl+Backspace 此階段為確認後由 client 刪詞。

取捨：確認後 client 游標在段末，不能保證句中位置貼上。Suspend/restore 需要
client 貼上完成及實際 selection 通知，通用協定未提供可靠操作。
Fcitx 呼叫順序不能證明不同 frontend 異步送達順序，必須另做桌面測試。
參考：
- https://fcitx-im.org/wiki/Develop_an_simple_input_method
- https://cgit.freedesktop.org/wayland/wayland-protocols/tree/unstable/text-input/text-input-unstable-v3.xml

## 階段與通過門檻

1. P0：共用 Buffer 確認路徑、移除平台 clipboard 攔截；覆蓋空白/未完成音節/
   長句/候選/句中插入及重複快捷鍵；原生編譯與回歸通過。
2. P1：組字內 Ctrl+Backspace 詞組刪除、狀態不變量、reconversion Escape 分層，
   評估前端 preedit 點擊定位；保護既有數字選字與局部修改。
3. P2：固定語料評估候選命中率、前後文排序及明確選字學習，驗證跨 context
   詞典合併與隱私。只對量測到的瓶頸引入快取。
4. P3：候選方向/數量/按鍵配置由 Buffer 與 UI 共用設定；主題、圓角、字型、
   HiDPI、螢幕邊界交由 Fcitx frontend，避免自建搶焦點浮窗。預設無動畫。
5. P4：量測每鍵 p50/p95、長句重建時間與記憶體，完成應用程式矩陣。

本次交付以 P0 為界；編譯或桌面驗證未通過前，不進行大規模 P1–P4 重寫。

## 第二階段增量

已落實應用程式編輯交接規則：Ctrl+A 等 client 快捷鍵先確認整段並離開編輯；
候選以外的 Tab/Shift+Tab、Shift+Delete 與 Shift+PageUp/PageDown 也採此規則。
保留候選內 Tab 導覽與 Shift+Delete 刪除學習紀錄；Ctrl+Space 僅接受 Ctrl。
已加入真實 Fcitx API 整合測試，修正 reconversion 的候選取消與空內容接受生命週期。
組字內詞組刪除、UI 設定與桌面矩陣仍未完成。

## 現代化 UI 增量

已加入 Fcitx V3 inline 點擊、預設停用重複編輯彈窗、可選候選方向、
原生 Classic UI 深/淺色圓角主題，以及備份既有設定的顯式套用指令。
新增 inline caret / emoji / 句中插入 / 主題設定測試，完整回歸通過。
多螢幕、實際 DPI 與不同 frontend 的外觀仍需桌面驗收；詳見 modern-ui.md。

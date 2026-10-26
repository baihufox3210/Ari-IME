# P0 驗證紀錄

## 環境與重現

2026-10-26，GCC 15.2、CMake 4.2、Fcitx5 5.1.19、libchewing 0.11.0。
SDK 與官方字典以 Ubuntu 套件解壓到 /tmp/ari-ime-sdk，未安裝或重新啟動使用者服務。
套件 CMake 的絕對 include/library 路徑只在 /tmp SDK 內調整。
本環境為 tty，沒有 DISPLAY / WAYLAND_DISPLAY，無法宣稱真實桌面整合通過。

建置設定：
```sh
env LD_LIBRARY_PATH=/tmp/ari-ime-sdk/usr/lib/x86_64-linux-gnu \
 PKG_CONFIG_LIBDIR=/tmp/ari-ime-sdk/usr/lib/x86_64-linux-gnu/pkgconfig \
 PKG_CONFIG_SYSROOT_DIR=/tmp/ari-ime-sdk \
 cmake -S . -B /tmp/ari-ime-build -DBUILD_TESTING=ON \
 -DCMAKE_PREFIX_PATH=/tmp/ari-ime-sdk/usr \
 -DPKG_CONFIG_EXECUTABLE=/tmp/ari-ime-sdk/usr/bin/pkgconf
cmake --build /tmp/ari-ime-build -j 4
env LD_LIBRARY_PATH=/tmp/ari-ime-sdk/usr/lib/x86_64-linux-gnu \
 CHEWING_PATH=/tmp/ari-ime-sdk/usr/share/libchewing \
 ctest --test-dir /tmp/ari-ime-build --output-on-failure
```

## 自動案例

新增 test_application_shortcut_handoff：每個快捷鍵測空組字、未完成 su、
44 字長段、候選開啟、句中插入並保留尾段。每例比對完整 commit、handled=false、
組字/候選清空與再次觸發不重複 commit。涵蓋 Ctrl+V、Ctrl+Shift+V、
Ctrl+C/X/A/Z/Shift+Z/Backspace、Shift+Insert/Left/End。
舊版會保留 preedit 且不 commit，無法符合新斷言。

第一次完整回歸：20390 checks，1 個舊語義失敗：舊測試要求應用程式 undo
交接時保留 preedit。已改成驗證先 commit「妳好」後清空；候選修正 undo
仍沿用既有測試驗證。最終結果見下方執行記錄。

| 情境 | 預期 | 自動化涵蓋範圍 |
|---|---|---|
| 未確認音節 Ctrl+V | 原始 su 確認，按鍵交還 client 一次 | Buffer + 真實 libchewing |
| 候選開啟 Ctrl+V | 確認顯示文字，關候選，放行 | Buffer + 真實 libchewing |
| 長句 Ctrl+V | 完整顯示文字不丟失、不重複 | 44 字長段 |
| 句中插入後貼上 | 含原尾段完整確認，client 游標段末 | Buffer |
| 方向鍵局部修改 | 保留其餘文字 | 既有 Buffer 回歸 |
| 數字/滑鼠選字後續輸入 | 共用選字路徑，保留其餘文字 | 既有 Buffer API 回歸；未測實際滑鼠 |
| Escape | 候選先關閉，普通組字維持既有取消規則 | 既有 Buffer 回歸 |
| Ctrl+Backspace | 確認後 client 刪詞 | 自動驗證交接；client 刪詞未測 |
| 中英快速切換 | 保留內容及模式設定 | 既有 Buffer 回歸 |

## 待執行的桌面整合驗收

下表各列均須在原生 Wayland 與 XWayland（程式可用時）各跑一輪。
記錄 compositor、應用程式版本、啟動參數、frontend、縮放與 protocol；不能只記程式名。

| 應用程式 | 預期 | 實際 |
|---|---|---|
| GTK 編輯器 / Firefox | 組字接在原文後，原樣貼上換行一次 | 未執行：無圖形 session |
| Qt 編輯器 | 同上；Shift+方向確認後由 client 選取 | 未執行 |
| Chromium / Electron / Discord | 候選開啟也能貼上，無遺留 preedit | 未執行 |
| Zed | 長句確認與貼上順序一致 | 未執行 |
| Warp / 一般終端機 | Ctrl+Shift+V 貼上一次，由 terminal 處理 bracketed paste | 未執行 |
| Wayland ↔ XWayland 切換 | context 隔離、不錯貼、不殘留候選 | 未執行 |

操作：先在 client 輸入 `A|B` 並置游標中間，複製 `貼上\n第二行`。
依序輸入 su、su3cl3、長句，分別在普通組字、Down 候選、Home/Right 句中修改後貼上。
預期 `A<當時顯示的完整組字>貼上\n第二行B`；應用程式若有格式或終端機貼上確認則依其規則。
再測空剪貼簿、富文字、emoji、按鍵重複、按下與釋放、滑鼠換游標、切焦點與 undo。
逐步記錄 commit 是否先到、貼上次數與插入點。不能用 Buffer 通過代替此驗收。

## 協定限制與未完成範圍

commitString + 原按鍵放行遵循 Fcitx engine 邊界，不人工讀取 clipboard 或偽造事件。
但 text-input-v3 的文字更新以 done 成批生效，不能從 C++ 呼叫順序推定所有
compositor/client 的文字事件與鍵盤事件交付順序。若某 frontend 實測倒序，需針對
該 frontend 查明序列化行為，不能以 sleep 或無條件重送 Ctrl+V 解決。
不支援可靠的句中 client 游標還原，也未實作 suspend/restore。

P1–P4（組字內 Ctrl+Backspace、新設定、視覺主題、效能量測）仍為後續工作，
本次沒有宣稱 UI 或跨應用相容性已全面完成。

## 最終執行結果

- 原生 Fcitx addon、字典工具、test_buffer 編譯成功。
- CTest：2/2 通過，25.41 秒。
- Buffer：20392 checks，0 failures；包含與真實 libchewing 的核心整合。
- 字典轉換：6 項測試通過。
- `git diff --check` 通過。
- Git bundle verify 通過，包含基線完整歷史。
- Linux frontend 事件整合與桌面 UI 手動驗收：尚未執行。

## 後續：編輯操作交接與錯誤修正

使用者確認：編輯中執行全選等應用程式操作，應先等同 Enter 確認並離開編輯，
再執行原操作。Ctrl+A 已具此路徑，新增檢查候選/游標完全復位及下次輸入重新組字。

新增測試在修改前實際重現 **41 個失敗斷言**（20460 checks）：
Tab/Shift+Tab 未確認、一般 Shift+Delete 誤刪組字、Ctrl+其他修飾鍵+Space 誤切模式。
已修正：Tab 在候選外確認並交接，在候選內仍切換候選；Shift+Delete 在候選外
交給 client 剪下，候選內仍刪除學習紀錄；模式鍵要求只有 Ctrl。

另修正平台 reconversion 的 Escape：第一次關候選並保留重選文字，第二次才取消
重選、恢復已從 client 移除的原文。避免平台覆蓋 Buffer 的分層取消規則。

新增 test_engine：真實 Fcitx Instance + engine + 記錄用 InputContext，直接驗證
Ctrl+A/V、Tab、candidate Tab、key release、commit 次數、client preedit 清空、
沒有額外 forwardKey，以及重選 Escape 的兩階段行為。這是平台 API 整合測試，
不是實際 Wayland compositor/GUI client；仍不能取代上述桌面矩陣。

修改前的工作樹 diff 保存在 `/tmp/ari-ime-before-edit-handoff.patch`，
搭配前述基線 bundle 可復原到本階段開始時的程式修改。

額外邊界錯誤：重選文字刪到空後按 Ctrl+A，原先 result.hasCommit=false，
平台仍留下 reconversionActive，後續 reset 可能把已刪除原文補回。
新增 KeyResult.applicationHandoff 明確標記「接受組字並交接」，包含空組字；
平台在此時清除重選交易。新增整合案例驗證刪光→全選→reset 不復活原文。

第二階段最終結果：原生編譯成功；CTest 3/3 通過（26.82 秒）；
Buffer 20516 checks / 0 failures，Fcitx engine 49 checks / 0 failures，
字典轉換 6 項測試通過；git diff --check 通過。
新增 Shift+PageUp/PageDown 交接也已包含在此輪回歸。

## 編輯器 undeclared keysym 診斷

`FcitxKey_KP_0` 同時存在於原生 keysymgen.h 與 WASM keysym.h。
缺少 compilation database 時，Clang 直接解析 buffer.cpp 會先報
`fcitx-utils/key.h file not found`。已新增 .clangd 指定 build，並讓 CMake
固定輸出 compile_commands.json，於工作樹 build/ 產生實際 SDK 參數。
Clang 21 的 clang-check 已檢查 8 個原生 source/test 編譯單元及其全部 target
組態，結束碼 0、沒有診斷。build/ 完整原生建置與 engine 整合測試通過。
此修正針對已重現的缺少編譯設定問題；未取得使用者編輯器原始完整診斷，
若仍有錯誤需比對第一個錯誤與該檔案實際使用的 compile command。

## 現代化 UI 與 inline 修正增量

顯示名稱 Ari Smart Bopomofo / Ari 智慧注音；保留內部識別、套件與資料路徑。
新增 Classic UI 深/淺色九宮格圓角主題、明確套用/備份工具、橫向候選選項。
編輯預覽改為 opt-in，預設只提供 inline caret；無 inline capability 仍顯示 panel preedit。
升級至 Fcitx V3，處理 client 回報的 Unicode codepoint 位置；點擊不確認整句，
而是定位到完整 grapheme 邊界，保留局部插入尾段。右鍵使用正常確認路徑。

新增測試實際發現候選查詢 scratch 被 mergeTail 再次 freeze、使句子重複的錯誤。
已修正：editing 狀態的 cells 已是完整句子，不再 freeze 該查詢 scratch。
測試也區分候選內 Enter（選取）與候選外 Enter（確認整句），避免錯誤的測試前置狀態。

最終 CTest 4/4 通過（27.12 秒）；之後補上空 XDG 環境變數路徑案例，
主題工具 7 項測試全部通過。更新的引擎、Buffer 與整合測試經 Clang 21 檢查無診斷。
原生建置成功；DESTDIR 安裝到 /tmp/ari-ime-ui-stage，確認兩套主題、圖片、
ari-ime-theme 與新名稱描述檔皆存在；安裝後 helper 成功在隔離 XDG 路徑套用深色與字型。
Shell 語法與 git diff --check 通過。

已保存 UI 修改前的 Git checkpoint `/tmp/ari-ime-before-ui-20261026.bundle`。
設計預覽是 SVG 示意圖，未宣稱為實際桌面截圖；真實 compositor/UI/DPI 驗證仍未執行。
完整操作與限制見 modern-ui.md；使用者日誌中的 chewing.dat 訊息尚未獲得版本與
詞典診斷輸出，未據此改寫/清除詞典或全域關閉 libchewing 錯誤記錄。

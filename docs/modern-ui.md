# Ari IME (baihu)：現代化組字與候選介面

[深／淺色設計預覽](ui-preview.svg)（示意圖，非桌面截圖）。

## 本次調整

輸入法顯示名稱還原為 **Ari IME (baihu)**，中英文使用同一名稱。
套件名、Fcitx 識別 `ari-ime`、設定與個人資料目錄保留，避免既有設定失聯。

預設移除編輯時額外的整句預覽與「游標 x/y」面板；插入位置透過 client preedit
的真實 caret 傳遞，不把 `|` 當作文字插入。按 Down/Up 重新選字時才開候選。
保留組字底線，表示這段文字尚未確認；插入線樣式與閃爍由應用程式繪製，
輸入法無法保證所有 GTK/Qt/終端機都使用相同的 caret 外形。

新增 `ShowEditingPreview=False`，有問題的 client 可在 Ari 設定開啟相容預覽。
既有 ShowStatusLine / ShowPendingZhuyin 是使用者主動開啟的輔助資訊，仍可產生面板。
不支援 inline preedit 的 client 保留 Fcitx 面板 preedit，避免組字變成不可見。

新增 `HorizontalCandidates=False`：可選直向或橫向候選。
候選以 frontend 的 list cursor 畫整列選取，取消重複的文字反白矩形。
候選標號縮小、固定留白；深色與淺色都有 12px 面板圓角、6px 選取圓角、細邊框。
幾何 PNG 採九宮格伸縮，由 Fcitx Classic UI 處理縮放、視窗位置與螢幕邊界，沒有額外動畫。

引擎升級使用 Fcitx V3 InvokeAction：支援此事件的 client 點擊組字可移動 caret，
不再採框架預設的 commit/reset；Unicode code point 位置轉換為 grapheme 邊界與
UTF-8 caret byte offset，避免拆開 emoji、變音符號。句中輸入仍保留尾段。
右鍵或無效位置走一致的確認流程。不支援點擊回報的 client 仍可使用方向鍵。

## 候選翻頁與寬度

Fcitx 引擎預設 `CandidateArrowKeys=ChangePage`：選單開啟時 Right 下一頁、Left
上一頁，首尾循環；只有一頁時保持原頁，不退出候選。PageUp/PageDown 仍可翻頁。
Esc 關閉候選並保留組字，之後 Left/Right 移動文字游標。Enter 確認目前候選。
不新增展開模式，原本數字／滑鼠選字與應用程式快捷鍵仍有效。

已儲存 `MoveCursor` 的舊設定優先於新預設；請在 Ari 設定的
「Left/Right in the candidate window」選「Turn candidate pages」並套用。

候選顯示層只替一至三字的短候選保留一個全形空格（U+3000）的字型寬度，
四字以上不加寬。橫排按整排候選總字數計算，超過三字不加寬；短排只在最後
一項後保留一次。「候選 1/4」是獨立頁碼列，不受候選字數門檻限制，
每次翻頁更新都在右側保留一個全形字寬。空格只存在顯示層，不寫入組字、
commit 或使用者詞典。

已移除共用面板的額外右側加寬。切換提示使用單字「中／英」，左右間距一致，
文字水平留白 8px、垂直 6px，以補償中文字寬和行高差異，回到接近方形的外觀。
實際外框由 Fcitx、字型與縮放量測，任意字型下不保證像素級正方形。

若切換時仍顯示整個輸入法名稱，請啟用緊湊模式：

```sh
ari-ime-theme dark --font 'Sans 12' --mode-hint compact
```

工具會分別備份 Classic UI 與全域設定，保留快捷鍵與其他區段，重新載入兩者。
這會設定共用的 `[Behavior] CompactInputMethodInformation=True`；
`--mode-hint full` 改回完整名稱，不指定則保留原值。
Fcitx 的緊湊提示會讀 Ari 提供的 subModeLabel，而非帶 baihu 的完整顯示名稱。

## 選字更新與反白穩定性

候選反白移動不再重送內容、格式與游標都相同的 client preedit。同一個字的
同一頁候選保留清單物件，只更新選取索引；翻頁、換字、候選內容或排列改變時
才建立新清單。文字與游標皆不變的操作不發送面板重繪事件，避免重設 hover。
提交、清除、游標移動及不支援 inline preedit 的前端仍依實際變更更新。

這項修改消除 Ari 的重複刷新路徑；應用程式自身的游標閃爍、DMS／Classic UI
繪製仍須桌面實測。若問題是應用程式內滑鼠拖曳反白，需要另記應用程式與操作。

## 用指令診斷閃爍

先更新／安裝本版 Ari，在 Niri 桌面終端機執行（以測試文字重現）：

```sh
fcitx5 -r -D --verbose='ari-ime=5,classicui=5' 2>&1 | tee ~/ari-ime-flicker.log
```

`-r` 取代目前的 Fcitx，`-D` 保持前景以便擷取。這個終端機先保持開著，
在另一個視窗輸入，記錄候選反白移動、翻頁、中英文切換時哪一步會閃。
完成後可從另一個終端機取出 Ari 記錄：

```sh
grep -E 'ARI_|Update component:|Input Window' ~/ari-ime-flicker.log | tail -n 160
```

Ari 的診斷預設關閉，啟用時只記錄狀態與長度，不記輸入文字或按鍵：

| 欄位 | 判讀 |
| --- | --- |
| preedit_changed | 1 表示文字、格式或組字游標改變，才發送 client preedit |
| panel_changed | 1 表示要求面板更新；只移動候選反白時應是 1 |
| list_rebuilt | 同字同頁只移動反白時應是 0；翻頁或候選改變可為 1 |
| context / frontend | 區分輸入上下文與 Wayland／其他前端 |
| ARI_CONTEXT program / display | 確認應用程式及顯示路徑，DBus 不等於原生 Wayland |
| client_panel | 1 表示 client 宣告自行畫候選視窗，不能只看 Classic UI 的視窗日誌 |
| relative_rect / scale / rect | client 的矩形座標語義、縮放及 x,y,寬,高；不是最終 popup 的尺寸或實際螢幕 DPI |
| event=cursor / focus_in / focus_out / reset | 記錄不經 Ari 按鍵處理也可能發生的幾何、焦點與重設事件 |
| page / pages / highlight | 對照反白移動及翻頁是否發生 |
| overlay_bytes | 新版 Fcitx 暫時模式提示的 UTF-8 長度，舊版沒有此欄位 API 時為 0 |
| ARI_MODE / ARI_RESET | 追蹤切換提示或組字被重設；重複 reset 需再看焦點／協定來源 |

只移動同頁反白，預期 `preedit_changed=0 panel_changed=1 list_rebuilt=0`。
若三項都是 0，Ari 沒有發出重繪；仍需對照 Fcitx 暫時提示、Classic UI 與應用程式。
日誌可以定位事件來源，不是單靠一行記錄就能斷定所有視覺閃爍的原因。

### Firefox + Niri：優先對照輸入協定

現場樣本在同頁移動反白時一直是 `preedit_changed=0 panel_changed=1 list_rebuilt=0`，
`frontend=dbus`；最後的隱藏發生在 `ARI_RESET` 之後。這證實重複組字更新已消除，
卻不足以判定候選視窗閃爍是 Ari、GTK popup 或合成器造成。

Fcitx GTK 模組可在 Firefox 程序內畫 client-side 候選視窗，使用 GTK 的 DPI；
它的 Wayland reposition 邏輯在游標矩形改變，或 popup 超過 parent 邊界時會
hide/show。後一項可能在每次候選反白更新時再次成立，因此即使組字不變仍可能閃爍。
這是已確認存在的上游程式路徑，是否命中本機須由 `client_panel` 與桌面對照確認。
Niri issue #3099 也有 Firefox 候選超出邊界閃爍、改走原生輸入協定後消失的回報。

先存好工作並從 Firefox 選單完全結束所有視窗，然後在 Niri 桌面終端機執行：

```sh
env -u GTK_IM_MODULE MOZ_ENABLE_WAYLAND=1 firefox
```

若仍有 Firefox 背景程序，指令可能只把請求交給舊程序，環境變數不會生效。
在 `about:support` 檢查 Window Protocol 是 `wayland`；這只確認顯示後端，
還需 Fcitx 日誌確認候選操作變成 `frontend=wayland_v2`，而非繼續 `dbus`。
若仍是 DBus，可再完全結束後以 `GTK_IM_MODULE=wayland` 明確選擇 GTK 內建模組：

```sh
env GTK_IM_MODULE=wayland MOZ_ENABLE_WAYLAND=1 firefox
```

用相同網頁輸入框、相同顯示器與縮放重現：

| 操作 | 預期 | 本環境實際結果 |
| --- | --- | --- |
| 句中開候選，以 Up/Down 移動反白 | 原生路徑不經 GTK 模組 popup 的 hide/show | 無桌面，待本機確認 |
| 輸入框靠近視窗底部再移動反白 | 候選由原生 input popup 正確定位，無持續閃爍 | 無桌面，待本機確認 |
| 中英文／輸入法切換 | 提示維持單字，記錄是否仍有異常放大 | 無桌面，待本機確認 |
| 有組字時 Ctrl+V | 組字先確認，應用程式貼上一次 | 引擎回歸通過；Firefox 待本機確認 |

若測試有效，在 `~/.config/niri/config.kdl` 的 `environment` 區段移除
`GTK_IM_MODULE "fcitx"`（不要整個刪除 environment），並檢查登入環境的其他設定來源；
登出／登入後完全重開應用程式。修改 Niri 設定前自行備份，執行 `niri validate` 檢查語法。
Wayland GTK3/4 通常會自行選內建 Wayland 模組；XWayland 程式如需 fcitx 模組，
可在 GTK 設定的 `[Settings]` 保留 `gtk-im-module=fcitx`，或對該程式個別設定環境。
保留 XWayland 的 `XMODIFIERS=@im=fcitx` 與適用 Qt 的設定。
此處不自動改動 Niri、DMS、Firefox profile 或全域縮放；若路徑已是原生仍閃，
需用新日誌的 cursor/focus 事件繼續定位，不能視為已修好。

依據：[Fcitx Wayland 設定](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland)、
[GTK3 popup 實作](https://github.com/fcitx/fcitx5-gtk/blob/master/gtk3/gtk3inputwindow.cpp)、
[Niri #3099](https://github.com/niri-wm/niri/issues/3099)。
停止前景診斷後，可用 `fcitx5 -r -d` 恢復一般背景執行；若由服務管理則依原服務啟動方式。

## 已轉換文字的刪除與游標

尚未按 Enter 的中文字也已完成轉換：Backspace 直接刪除游標前的中文字，
不再把逆序／聲調提前輸入的音節還原成英文字母。剩餘中文保留目前顯示結果，
可繼續選字、輸入與提交。尚未轉成中文的音節仍逐鍵刪除。

滑鼠位置先從 Unicode code point 轉成 grapheme，再映射為組字資料格位置；
不再把畫面字數直接當作資料格索引。點擊「……」後方或兩個省略號中間時，
插入與刪除依實際點擊位置執行，emoji／組合字不會被拆開。

## Zed Wayland 的限制與修正

已定位 Zed 的 Wayland backend 丟棄 preedit cursor 欄位，更新 marked text 時
也未提供選取位置。單純更新 Ari 無法讓既有 Zed 讀取這些位置。
Zed 的游標修正改由獨立 fork 處理，Ari 專案不再附帶或維護 Zed 補丁。

## DMS + Niri topbar 圖示

Quickshell 原始碼中的黑紫棋盤格是找不到圖示時的 missingPixmap。
這與 Niri 視窗定位無直接關係。先前只驗證 Fcitx 的圖示查找成功，
無法證明 DMS 所用的 Qt 圖示主題也能解析相同名稱。

Ari 現在透過 Fcitx subModeIcon 回傳已安裝 128px PNG 的絕對路徑，讓
DMS／Quickshell 的 QIcon::fromTheme 直接載入檔案，避開主題名稱搜尋和負快取。
路徑由 Fcitx XDG Data 搜尋取得，支援一般系統與使用者安裝；若檔案不存在，
退回標準 input-keyboard 圖示。SVG 及各尺寸 PNG 仍安裝供一般選單使用。

更新引擎後需要重啟 Fcitx（安裝腳本會處理）。明確選用圖片模式：

```sh
ari-ime-theme dark --font 'Sans 12' --tray-icon image
```

此選項會備份並調整共用的 PreferTextIcon 設定，不指定就保留原值。
`--tray-icon text` 可改回文字。測試請聚焦文字欄位並切到 Ari；英文鍵盤狀態的
圖示由 Fcitx keyboard addon 提供，不是 Ari 的 subModeIcon。

若 Ari 啟用後仍有棋盤格，請在桌面終端機讀取實際 SNI IconName：

```sh
busctl --user get-property org.fcitx.Fcitx5 /StatusNotifierItem org.kde.StatusNotifierItem IconName
```

圖片模式下應為 PNG 的絕對路徑；若仍是舊名稱，先確認新模組已載入。
若路徑正確則需保留 DMS／Quickshell 版本與日誌，進一步確認面板端渲染。
本環境沒有 DMS/Niri session，尚不能宣稱已完成實機驗收。

依據：
- [Quickshell missingPixmap](https://github.com/quickshell-mirror/quickshell/blob/master/src/core/iconimageprovider.cpp)
- [Quickshell SNI icon](https://github.com/quickshell-mirror/quickshell/blob/master/src/services/status_notifier/item.cpp)
- [Qt absolute-path QIcon](https://github.com/qt/qtbase/blob/6.10/src/gui/image/qicon.cpp)

## 在 Arch 安裝這個 checkout

請在一般使用者的桌面終端機執行。若先前出現「不是 Git 版本庫」或找不到腳本，
代表當時不在原始碼目錄；從新的目錄下載即可：

```sh
git clone https://github.com/baihufox3210/Ari-IME.git ~/Ari-IME-modern
cd ~/Ari-IME-modern
bash scripts/build-from-source.sh --deps --install
ari-ime-enable
```

若已下載則進入實際 checkout，再 `git pull --ff-only`。
不直接執行上游 PKGBUILD 的 makepkg：該檔案下載發行版，未必包含目前 checkout。
專案腳本會使用目前 checkout 打包，經 pacman 安裝並重新啟動 Fcitx。

套用固定深色（會備份原 Classic UI 設定）：

```sh
ari-ime-theme dark --font 'Sans 12'
```

選項：`light` 固定淺色；`auto` 設定亮/暗主題並請 Fcitx 跟隨桌面配色。
自動配色取決於 desktop portal 是否回報色彩偏好；沒回報時可用固定 dark。
不指定 `--font` 就保留目前字型，字型大小也可在 Fcitx Classic UI 設定修改。
`--dry-run` 只預覽設定，`--no-reload` 不要求 Fcitx 重新載入。

這是 **Fcitx Classic UI 共用主題**，套用會影響使用該 renderer 的其他輸入法。
安裝套件本身不會擅自更換使用者主題。Kimpanel、GNOME 擴充或其他 client renderer
若接管候選 UI，會使用自己的樣式；這時需在 Fcitx addon 設定選用 Classic UI。
不要只因日誌出現 Loaded addon kimpanel 就判斷它目前正在繪製候選。

設定檔為 `~/.config/fcitx5/conf/classicui.conf`（尊重 XDG_CONFIG_HOME）；
備份檔會顯示在指令輸出。複製回原位置後，執行下列指令重讀主題：

```sh
busctl --user call org.fcitx.Fcitx5 /controller org.fcitx.Fcitx.Controller1 ReloadAddonConfig s classicui
```

`fcitx5-remote -r` 只重新載入全域設定，不能取代上述 addon 重新載入。
主題工具現在直接呼叫 ReloadAddonConfig，並查詢 CurrentUI。只有重讀成功且
目前 renderer 是 classicui 時才回報成功；未連線或 kimpanel 接管時會明確提示並
傳回非零結束碼。這不代表螢幕像素已驗證；請開候選確認實際外觀。

若主題沒有變，請提供工具完整輸出及下列唯讀診斷：

```sh
printf '%s\n' "$XDG_CURRENT_DESKTOP" "$XDG_SESSION_TYPE"
busctl --user call org.fcitx.Fcitx5 /controller org.fcitx.Fcitx.Controller1 CurrentUI
```

游標不動時請記錄應用程式名稱、版本，以及是在連續輸入或 Left/Right 編輯時發生。
引擎測試驗證每次前端更新攜帶的 UTF-8 cursor offset；它不能證明應用程式真的繪製該游標。
只重新載入設定足以套用主題；更新引擎本身仍須重新啟動 Fcitx。

## 驗證

- 輸入 su3cl3，不按 Enter；Left/Home/End 移動時不應出現第二份句子或游標面板。
- Down 開候選，藍色整列標示目前選項；Escape 回到只有 inline caret。
- 支援 Fcitx InvokeAction 的編輯器中點擊句中，輸入新字後原尾段應保留。
- 測試中/英/emoji 混合、句中重新點擊、滑鼠候選與 Ctrl+A/V。
- 深色/淺色、100%/150%/200%、螢幕邊界與雙螢幕逐項確認；保留原生縮放設定。

自動化涵蓋 inline caret 位元組位置、無輔助彈窗、候選方向、相容預覽、
無 inline client 的文字可見性，以及點擊句中/ZWJ emoji/候選/右鍵的狀態生命週期。
Python 測試驗證主題套用保留原設定、精確備份、冪等、symlink 與不落盤預覽。
本環境沒有 compositor，真實候選視窗的外觀與多螢幕定位仍待上述桌面實測。

## 日誌中的 chewing.dat

這個訊息不能單憑一行就判定是主字典或使用者詞典損毀。不同 libchewing 版本
對可選詞典的載入/記錄方式不同：較新版的 loader 會記錄找不到某個資產後繼續。
目前輸入已可轉換中文，只能證明引擎與部分詞典可工作，不能證明學習持久化正常。
不要建立空 chewing.dat 或清除個人資料來消除訊息。若持續出現請記錄：

```sh
pacman -Q fcitx5 libchewing fcitx5-ari-ime
ari-ime-dict info
ari-ime-dict candidates su3
```

再測明確選字、Enter 接受、重啟後候選是否保留偏好。若主字典確實缺少，
依 pacman 套件檔案清單與字典診斷修復，而非掩蓋所有 stderr。

參考（上游原始碼與規格）：
- https://fcitx-im.org/wiki/Fcitx_5_Theme
- https://github.com/fcitx/fcitx5/blob/master/src/lib/fcitx/inputmethodengine.cpp
- https://github.com/fcitx/fcitx5-qt/blob/master/qt5/platforminputcontext/qfcitxplatforminputcontext.cpp
- https://github.com/chewing/libchewing/blob/master/src/dictionary/loader.rs

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

已移除共用面板的額外右側加寬，InputPanel 和 Menu 使用對稱邊距，避免影響
切換輸入法提示及清單。Ari 自己的中英文切換提示縮短為「中／英」。
實際提示框由 Fcitx 按字型量測，對稱邊距不保證所有字型下外框像素恰好正方形。

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
已提供 [Zed 修正補丁與驗證步驟](../contrib/zed/README.md)，需要編譯修正後的 Zed。
補丁已檢查可套用，UTF-8→UTF-16 轉換的 Rust 測試通過；整個 Zed 尚未編譯／桌面驗收。

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

# Zed：Wayland 組字游標位置補丁

Zed 的 Wayland backend 在 `PreeditString { text, .. }` 中丟棄 `cursor_begin` /
`cursor_end`，並以 `replace_and_mark_text_in_range(None, &text, None)` 更新編輯器。
因此即使 Ari/Fcitx 傳出正確位置，Zed 仍不能顯示組字中間的插入位置。

這份補丁：

- 與文字一起保存同一個 preedit 事件的選取範圍，在 Done 時原子套用。
- 將 Wayland 的 UTF-8 位元組索引轉為 GPUI 使用的 UTF-16 code unit 索引。
- 把選取範圍傳給 marked-text handler，支援只改游標、文字不變的更新。
- 保留 dead-key compose 原有 SetMarkedText 路徑。
- 負值、越界與非 UTF-8 邊界回退為既有的未指定選取位置行為。

基準 commit 見 [BASE_REVISION](BASE_REVISION)：
`e0f4f28001da631f4e37f2155bbc7f43a652a37e`。
此補丁修改的是 Zed，不會因安裝 Ari 套件而自動修改已安裝的 Zed。

## 已驗證與限制

- 對基準兩個原始碼檔執行 `git apply --check`：通過。
- 從補丁抽出的實際轉換函式和 3 個 Rust 測試以 rustc 1.93 編譯執行：通過。
  涵蓋中文、反向選取、非 BMP emoji、ZWJ、負值與無效索引。
- 尚未編譯整個 Zed／GPUI，也沒有 Niri 桌面實測。
  必須完成下列 crate 測試及實際輸入，才能認定補丁在目標版本可用。

## 在另一個目錄編譯測試

先在 Ari-IME 原始碼目錄執行；另開 checkout，不修改原本的 Zed 安裝。
需要 Git、Rust/rustup 與 Zed 官方要求的 Linux 建置依賴。

```sh
ari_source_dir="$PWD"
git clone https://github.com/zed-industries/zed.git ~/zed-ari-cursor
cd ~/zed-ari-cursor
git checkout --detach e0f4f28001da631f4e37f2155bbc7f43a652a37e
git apply --check "$ari_source_dir/contrib/zed/wayland-preedit-cursor.patch"
git apply "$ari_source_dir/contrib/zed/wayland-preedit-cursor.patch"
./script/linux
cargo test -p gpui_linux preedit_cursor
cargo run
```

這裡啟動開發版測試；不執行安裝腳本，也不取代 Arch 套件。
若使用其他版本，先 `git apply --check`；失敗時不要強制套用，需要按版本調整。

實機驗收：

1. 在 Niri Wayland 上開啟編輯器（先關閉 Vim mode 以隔離模式影響）。
2. 輸入 `su3cl3`，不按 Enter，依次 Home / Right / End，插入線應位於「你好」的
   前方／兩字中間／後方，文字保持不變。
3. 中間插入中文，再 Backspace，尾段保留，插入線跟著組字位置更新。
4. 混合 emoji／中文，位置不拆開 surrogate pair；輸入、選字、Enter 與 Ctrl+V
   不得重複或丟失文字。
5. 分別測編輯器與 Zed 內建終端機。兩者的 input handler 不同，不假設結果相同。

未編譯補丁時，可先按 Enter 完成組字，再用 Zed 的方向鍵編輯已提交文字。
這會結束該段組字；不能視為組字中間編輯已修復。

## 來源

- [基準 Wayland client](https://github.com/zed-industries/zed/blob/e0f4f28001da631f4e37f2155bbc7f43a652a37e/crates/gpui_linux/src/linux/wayland/client.rs)
- [基準 marked-text handler](https://github.com/zed-industries/zed/blob/e0f4f28001da631f4e37f2155bbc7f43a652a37e/crates/gpui_linux/src/linux/wayland/window.rs)
- [Zed 官方 Linux 建置說明](https://zed.dev/docs/development/linux)

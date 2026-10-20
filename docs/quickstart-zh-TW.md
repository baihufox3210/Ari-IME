# Ari-IME 快速上手

Ari-IME 是 Fcitx5 的繁體中文注音輸入法，讓注音和英文留在同一段可編輯
文字裡，不必先切換中英文模式。

例如輸入：

```text
aceru/6aj4
```

組字區會顯示「acer螢幕」。按 Enter 後，整段文字才會送到目前的程式。

## 安裝

Arch Linux 使用者可安裝預先編譯套件：

```sh
yay -S fcitx5-ari-ime-bin
ari-ime-enable --yes --make-default
```

Ubuntu／Debian 使用者可從 [GitHub Releases](https://github.com/kaiyasi/Ari-IME/releases) 下載 `.deb` 套件安裝，再執行：

```sh
ari-ime-enable --make-default
```

安裝後重新啟動 Fcitx5，並在輸入法清單選取 **Ari IME**。其他發行版、NixOS
和手動建置方式請看[完整安裝說明](../README.md#install-on-arch-linux)。

## 先試三件事

1. 輸入 `su3`，組字區顯示「你」。注音音節完成後會直接顯示目前結果，不會自動跳出候選窗。
2. 輸入 `aceru/6aj4`，確認英文和中文可以留在同一段文字裡。
3. 按 ↓ 開啟候選選字，選取候選後按 Enter 送出整段文字。

未完成的注音仍會顯示原始按鍵；例如 `su` 會先保留為 `su`，完成聲調後才
轉成中文。音節尚未完成時，Space 可輸入一聲；沒有待完成音節時則輸入空格。
可選的傳統 Space 選字模式可在 Fcitx5 附加元件設定中開啟。

## 常用操作

| 操作 | 說明 |
|---|---|
| `↓` | 在游標處開啟候選，亦可用數字鍵或滑鼠選取 |
| `←`、`→`、`Home`、`End` | 在尚未送出的文字中移動游標，修正前面輸入的字詞 |
| `Enter` | 送出整段組字文字 |
| `Esc` | 關閉候選，或清除組字文字 |
| `Ctrl+Space` | 切換強制英文模式 |
| `Ctrl+Alt+R` | 在支援周邊文字的程式中，重新選取短中文字串並修正候選 |
| `Shift+Delete` | 忘記目前反白的個人候選 |

一般標點預設保留半形；按住設定的中文標點快捷鍵再按標點鍵，才輸入中文
標點。各項快捷鍵和注音鍵盤排列都能在 Fcitx5 設定中調整。

## 個人化與隱私

Ari 使用本機詞語模型和個人學習資料，不需要網路或帳號。敏感欄位不會寫入
學習資料；也可以在附加元件設定中關閉自動學習。個人詞語可用
`ari-ime-dict` 匯出、匯入和備份。套件安裝不會匯入或更動既有個人資料。

```sh
ari-ime-dict info
ari-ime-dict export "$HOME/ari-ime-dictionary.tsv"
ari-ime-dict import --dry-run "$HOME/ari-ime-dictionary.tsv"
```

詞語候選排序會受發行版提供的 libchewing 版本和本機學習資料影響；不同電腦
的第一候選可能不同。Ari 會保留個人明確選過的詞，但不保證各發行版的候選
順序完全一致。

## 從唯音／小麥注音轉移詞庫

Ari 提供轉換工具，可將使用標準注音讀音的唯音或小麥注音 TSV 轉成 Ari
格式。轉換會把音節間的連字號改成空格，並正規化輕聲符號；原詞庫權重不會
帶入 Ari。拼音讀音會列為不支援項目，不會猜測轉換。

```sh
ari-ime-convert-vchewing-dictionary old-phrases.tsv --dry-run
ari-ime-convert-vchewing-dictionary old-phrases.tsv ari-phrases.tsv
ari-ime-dict import --dry-run ari-phrases.tsv
ari-ime-dict import ari-phrases.tsv
```

從原始碼目錄執行時，也可用
`python3 scripts/convert_vchewing_dictionary.py` 取代已安裝的指令名稱。

預設遇到無法轉換的資料時不會產生輸出檔。檢視錯誤後，若確定要只匯入有效項目，可加上 `--skip-invalid`。已存在的輸出檔需明確加上 `--force` 才會覆寫。

## 回報問題

回報候選問題時，可附上 `ari-ime-dict info` 和 `ari-ime-dict candidates su3`
的輸出，並說明桌面環境、Wayland／X11、程式工具組，以及 Ari 和 libchewing
版本。命令不會讀取目前 Fcitx5 組字內容；請不要附上私人詞庫或密碼欄位文字。

完整功能、安裝方式和開發說明請看[英文 README](../README.md)。

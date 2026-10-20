# Known Limitations and Field Reports

This file tracks known limitations, fixed field reports, and environment-
dependent behavior that affects how Ari IME behaves in the field. See
[CHANGELOG.md](CHANGELOG.md) for fixed issues and release history.

## GitHub issue #9: Shift+Delete on Hyprland/Wayland

The reporter observed this on Ari IME 2.5.4. Modifier-only key events now leave
candidate selection open; the fix shipped in 2.6.3, and 2.7.2 includes a
regression test for the standalone Shift followed by Shift+Delete sequence.
GitHub now reports the [public issue](https://github.com/kaiyasi/Ari-IME/issues/9)
as closed. The live Hyprland check is still unchecked in
[docs/manual-qa.md](docs/manual-qa.md); issue closure does not prove that this
compositor-specific path was manually verified.

## GitHub issue #10: Conservative typo correction

The requested `HHK$G$` and `GHK$G$` recovery is already included in the 2.6.3
release. Ari removes a repeated key or one invalid leading key only when the
remaining input independently converts to Chinese, preserving ordinary
English such as `price$`. The buffer regression suite covers both examples,
uppercase and shifted-key input, and incomplete prefixes; the manual QA
checklist includes the same examples. The [public report](https://github.com/kaiyasi/Ari-IME/issues/10)
has been closed as completed with this release and test evidence.

## libchewing version sensitivity

Ari IME does not own libchewing's exact candidate ordering. Different libchewing
versions can rank homophones/phrases differently, so the *first* candidate shown
may vary between distributions:

- The Arch release gate uses the current Arch libchewing package and prints its
  resolved version in the build log; the exact version can change independently
  of Ari.
- Ubuntu/Debian build against whatever the distribution ships (for example
  **libchewing 0.8.x** on Ubuntu 24.04).

The automated tests are written to tolerate ranking differences where Ari IME
does not control the order. Explicit choices receive extra libchewing learning
passes and are recorded in Ari's sidecar (`preferences.tsv`) for portable
bookkeeping, but live candidate order remains under libchewing's contextual
and frequency scorer. Ari does not scan a hidden candidate window during normal
typing. The Ubuntu CI job (`.github/workflows/ubuntu.yml`)
prints the resolved `chewing` version so ordering differences can be correlated
with dependency drift.

## Engine initialization failure is degraded, not fatal

If the libchewing context cannot be created (missing system dictionary, or an
unwritable user-dictionary directory), Ari IME:

1. retries with a read-only chewing context (losing only per-user learning), and
2. if that still fails, degrades to plain-English passthrough so keystrokes stay
   visible instead of being silently swallowed, showing a one-time hint
   (「注音引擎載入失敗，暫以英文輸入」).

Set `CHEWING_PATH` if libchewing's system dictionary is in a non-standard
location.

## Learned-dictionary location (libchewing 0.12+)

Recent libchewing versions store learned phrases (`chewing.dat`,
`chewing-deleted.dat`) at
`CHEWING_USER_PATH`, falling back to `$XDG_DATA_HOME/chewing` — not at the
`userpath` file passed to `chewing_new2`. Ari pins `CHEWING_USER_PATH` to its own
data directory (`~/.config/ari-ime` by default) so learning stays self-contained
and does not pollute the shared chewing directory used by other libchewing input
methods (fcitx5-chewing, ibus-chewing).

Older Ari builds did not pin this, so learned data may still sit in
`~/.local/share/chewing`. `scripts/reset-user-data.sh --include-shared` clears
that shared location; without the flag it only prints a notice, since the
directory may be shared. Reset the learned dictionary with
`scripts/reset-user-data.sh`.

`ari-ime-dict export` transfers personal phrase mappings in a portable text
format. It intentionally does not promise byte-for-byte preservation of
libchewing frequency or deleted-entry state across different libchewing
versions; use `ari-ime-dict backup` for same-engine raw recovery.

## Real-application rendering depends on the desktop session

Pre-edit display, candidate windows, clipboard behavior, and theme rendering are
provided by the client toolkit and Fcitx5 theme, not by Ari IME. These still
need manual validation across toolkits and display servers; the matrix and steps
live in [docs/manual-qa.md](docs/manual-qa.md). Current status of that matrix is
tracked there.

## Committed-text reconversion depends on surrounding text

`ReconversionKey` only acts when the client exposes a valid selected range
through Fcitx5's `SurroundingText` capability. Ari intentionally passes the
shortcut through unchanged for clients without that capability, mixed or
non-Han selections, selections longer than 32 Chinese characters, and
password/sensitive fields. The first external short selection may take a
moment while Ari finds safe readings through libchewing's candidate tables;
readings already composed by Ari and repeated reverse lookups are cached in the
input context. GTK, Qt, and Electron applications still need the manual
surrounding-text checks in [docs/manual-qa.md](docs/manual-qa.md).

## Long pre-edit context has a finite window

Ari keeps the whole mixed string editable, but libchewing's contextual model has
a finite active window. The public build keeps up to 32 Chinese characters in
that window; older characters remain editable cells, but phrase ranking across
that boundary may not use the full sentence context.

## Pinyin layouts are intentionally unsupported

The state machine is built around one-key-per-Bopomofo-symbol layouts, so
Pinyin keyboard modes are deliberately not exposed.

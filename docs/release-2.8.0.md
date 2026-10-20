# Ari IME 2.8.0

This feature release makes personal dictionary migration easier and adds a
local browser demo and candidate-coverage benchmark.

## Portable dictionaries

- `ari-ime-dict` now exports and imports one canonical Bopomofo syllable per
  phrase character, separated by spaces. This supports multi-character entries
  and fourth-tone readings while rejecting malformed or unsegmented input.
- A new converter accepts VChewing/XiaoMai-style Bopomofo TSV files, translates
  hyphenated syllables to Ari's space-separated form, and normalizes light-tone
  aliases. Pinyin rows are reported instead of guessed; source weights are not
  transferred. The converter is installed as
  `ari-ime-convert-vchewing-dictionary` with the native packages.
- Conversion supports dry-run validation, explicit partial import, duplicate
  reporting, and protection against accidental output replacement.

## Try and compare Ari

- A Traditional Chinese quick-start guide covers installation, mixed input,
  personal data, and dictionary migration.
- A local browser demo runs the checked-in WebAssembly runtime without sending
  typed text to a server.
- An isolated candidate benchmark reports the Ari binary and libchewing
  versions and checks candidate coverage without touching the active personal
  dictionary.

## Packages and validation

CMake, Arch, Debian, and `@ari-ime/wasm` package versions are synchronized at
2.8.0. The WASM runtime was rebuilt from the checked-in core with Emscripten
6.0.7 and the project-pinned libchewing 0.8.5 source.

Validation includes the release build and CTest suite, converter safety tests,
candidate benchmark, and a Node.js mixed-input smoke test. The manual Hyprland
`Shift+Delete` compositor check remains unchecked in
[`docs/manual-qa.md`](manual-qa.md); the regression suite cannot verify real
Wayland modifier delivery.

# Ari IME 2.7.1

This release fixes punctuation boundaries in mixed Bopomofo and English input.

## Input behavior

- `/su3` now becomes `/你`: a leading slash remains punctuation when the keys
  after it independently make a Chinese syllable. The same rule applies after
  text, as in `abc/su3` → `abc/你` and `su3/su3` → `你/你`.
- A slash within a valid syllable remains a Bopomofo key, so input such as
  `u/6` continues to convert. URLs and other technical literals remain intact.

## Packages

The native Fcitx5 packages and the rebuilt `@ari-ime/wasm` runtime are version
2.7.1. The GitHub release pipeline publishes Arch and Debian packages plus a
versioned WebAssembly package archive when the `v2.7.1` tag is pushed.
The release and sanitizer CI jobs run the published buffer regression test
even when maintainer-only test files are absent from a clean checkout.

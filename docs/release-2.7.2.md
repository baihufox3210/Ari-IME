# Ari IME 2.7.2

This release fixes contextual conversion after learning a single-character
candidate.

## Input behavior

- A deliberate single-character choice still teaches libchewing, but no longer
  becomes an Ari phrase preference that overrides every occurrence of the same
  reading. For example, a preference for 「值」 no longer changes 「智慧」 to
  「值會」.
- Preferences for phrases of two or more characters continue to work.
- Regression coverage now checks that a standalone Shift event leaves the
  candidate window open and that Shift+Delete enters candidate forgetting.
  The modifier handling itself was included in Ari IME 2.6.3.

## Packages

The Arch and Debian packages and the rebuilt `@ari-ime/wasm` runtime are
version 2.7.2.

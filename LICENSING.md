# Licensing

From this revision onward, Kaiyasi's first-party Ari IME source code is licensed
under the **KYL Noncommercial License, version 1.0**, in [LICENSE](LICENSE).
This notice expressly applies that license to the project's first-party source
files, except for third-party material and previously built artifacts below.

Noncommercial use is permitted. Commercial use, including internal use by a
for-profit entity, paid client work, and monetized services, requires a separate
written agreement. Contact zengeric0315@gmail.com for rights controlled by Kaiyasi.
Distributed modifications of covered files must retain this license and include
the corresponding source as described in the license.

## Third-party components and earlier versions

Fcitx5, libchewing, dictionary data, toolchain runtime code, and any other
third-party components retain their own licenses. See
[the WASM third-party notices](wasm/THIRD_PARTY_NOTICES.md). This change does not
relicense those components or waive their distribution and relinking requirements.
The local minimal Fcitx compatibility shims in `wasm/include/` are Ari source,
not a relicensing of upstream Fcitx5 headers or libraries.

The previous revision was `43718d193ea222af8a2f59442eea48c93bddbe88`. Rights already granted under GPL for earlier
versions remain unchanged. The checked-in generated artifacts in `wasm/dist/`
were built before this transition; their existing licenses remain unchanged.
Their Ari portions retain the earlier GPL-3.0-or-later licensing, and their
third-party portions retain their respective licenses. New first-party source
changes are not offered under GPL merely because those older artifacts remain.

The prior GPL text is retained in `licenses/legacy/GPL-3.0.txt` and in
`wasm/licenses/GPL-3.0.txt` for historical notices and the existing artifacts.
Future binary distributions must identify their source revision and preserve all
applicable third-party licenses and source/relinking rights.

This is a custom source-available license, not an OSI-approved open source license.

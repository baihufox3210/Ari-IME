# Third-party notices

The generated `dist/` artifacts include the libchewing 0.8.5 C backend and its
dictionary data. libchewing is distributed under the GNU Lesser General Public
License, version 2.1 or later. Its source and license are available at:

<https://github.com/chewing/libchewing/tree/v0.8.5>

The Emscripten compatibility patch used to rebuild that dependency is included
in `patches/libchewing-0.8.5-emscripten.patch` in the repository source tree.

## Existing prebuilt Ari artifacts

The checked-in `dist/` artifacts were built before the KYL transition. Their
Ari portions retain GPL-3.0-or-later; a copy is included in
`licenses/GPL-3.0.txt`. The current first-party source is offered under
KYL Noncommercial 1.0 in `LICENSE`. This does not change the licenses of
libchewing, its dictionary data, or other third-party runtime components.

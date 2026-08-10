# Distribution Checklist

This repository is structured so the HolyFramework source corresponding to a
distributed binary can be published together with the relevant dependency
source and license notices.

## When publishing HolyFramework

1. Publish the complete HolyFramework source for the released version.
2. Keep `src/`, `include/`, `F4SE/Plugins/HolyFramework.toml`, and `xmake.lua`.
3. Keep the complete vendored `lib/commonlibf4/` tree, including
   `lib/commonlibf4/lib/commonlib-shared/`.
4. Keep all root legal files:
   - `LICENSE`
   - `EXCEPTIONS`
   - `SDK_LICENSE`
   - `THIRD_PARTY_NOTICES.md`
   - `MODULE_LICENSING.md`
5. Preserve all third-party license and exception files inside `lib/`.
6. When publishing a compiled DLL separately, identify the exact source tag
   or commit that corresponds to that binary.
7. Do not publish generated build output as a substitute for source.

## Source/binary matching

For each public release, a recommended pattern is:

- Git tag: `v0.35.0`
- Binary release: HolyFramework v0.35.0
- Source: the exact repository contents at tag `v0.35.0`

If the source changes after a binary is built, create a new version/tag rather
than silently replacing the source associated with the old binary.

## Modules

Modules that use only the public HolyFramework SDK should carry their own
license and notices.

They do not need to vendor CommonLibF4 merely because HolyFramework itself
uses CommonLibF4. A module that directly uses CommonLibF4/commonlib-shared
must separately comply with those projects' licenses.

## No warranty

This checklist is intended to make compliance and source availability easier.
It is not legal advice and cannot replace review of the authoritative license
texts.

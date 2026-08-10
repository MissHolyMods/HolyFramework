# HolyFramework

HolyFramework is a shared native runtime framework.
It centralizes reusable game/runtime mechanisms so project modules can request
capabilities through a stable public ABI instead of duplicating direct
CommonLibF4/F4SE access.

## Building

Requirements:

- CommonLibF4 from https://github.com/libxse/commonlibf4
- C++23-capable MSVC or Clang-CL toolchain
- Latest available xmake version (3.0.0 or newer)

## Repository layout

- `include/HolyFramework/` — public SDK/ABI
- `src/` — HolyFramework implementation

## Distribution

See `DISTRIBUTION.md`.

## Module authors

Use only the public headers API in `include/HolyFramework/`.

See `MODULE_LICENSING.md` for licensing guidance.

[Module repository](https://github.com/MissHolyMods/Modules-HolyFramework)

## Disclaimer

The licensing documents in this repository are intended to state the
permissions granted by the HolyFramework copyright holder(s) and preserve
third-party notices.

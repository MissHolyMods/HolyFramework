# Third-Party Notices

HolyFramework includes or builds against third-party software. This file is a
summary only; the authoritative license texts are the license files shipped
with each component.

## CommonLibF4

Vendored path: `lib/commonlibf4/`

The exact CommonLibF4 revision supplied with this HolyFramework source tree
carries the MIT License at `lib/commonlibf4/LICENSE`.

A convenience copy is also provided at:

`LICENSES/CommonLibF4-MIT.txt`

The vendored CommonLibF4 build declares `commonlib-shared` as a public static
dependency.

## commonlib-shared

Vendored path: `lib/commonlibf4/lib/commonlib-shared/`

This component is distributed under GNU GPL version 3 together with the
additional permissions in its `EXCEPTIONS` file.

Authoritative copies in the vendored source tree:

- `lib/commonlibf4/lib/commonlib-shared/LICENSE`
- `lib/commonlibf4/lib/commonlib-shared/EXCEPTIONS`

Convenience copies are also provided under `LICENSES/`.

Do not remove or replace those upstream notices when redistributing the
vendored source.

## spdlog

The vendored commonlib-shared build requests `spdlog v1.16.0` through xmake.
spdlog is distributed under the MIT License.

A copy of the v1.16.0 license notice is provided at:

`LICENSES/spdlog-v1.16.0-MIT.txt`

## Optional commonlib-shared packages

The commonlib-shared build file defines optional support for SimpleIni,
glaze, toml11, and xbyak. These options are disabled by default in the
vendored source. If you enable and distribute a build using one of those
packages, review and comply with that package's license as well.

## Fallout 4 and F4SE

Fallout 4 and the F4SE runtime are third-party software and are not included
in this source distribution. Their names are used only to describe
compatibility and runtime requirements.

## No relicensing of third-party code

The HolyFramework root `LICENSE`, root `EXCEPTIONS`, and `SDK_LICENSE` apply
only to code for which the HolyFramework copyright holder(s) have the right
to grant those terms. They do not replace licenses carried by third-party
source under `lib/`.

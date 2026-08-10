# Module Licensing

HolyFramework is intentionally designed so project modules can communicate
with Fallout 4 through the public HolyFramework ABI instead of directly
depending on CommonLibF4.

## Public SDK

The public SDK is:

`include/HolyFramework/`

Those headers are additionally available under the MIT License in
`SDK_LICENSE`.

They are designed to remain free of CommonLibF4/F4SE/RE/REL/REX ABI types.

## Independent modules

The root `EXCEPTIONS` file grants additional permission, under the copyright
held in HolyFramework, for an Independent Module that communicates solely
through the public HolyFramework ABI to use a license chosen by the module
author.

This is intended to permit both open-source and closed-source modules without
making them GPL-covered solely because they use HolyFramework's public ABI.

## What a module should avoid

If you want a module to remain independent of CommonLibF4/commonlib-shared
license obligations, do not:

- include CommonLibF4 headers in the module;
- use `RE::*`, `REL::*`, `REX::*`, or CommonLibF4/F4SE game-access APIs
  directly;
- statically or dynamically link the module directly against CommonLibF4 or
  commonlib-shared;
- copy implementation code from HolyFramework or GPL-covered dependencies
  into the module;
- install direct game hooks that should instead be requested through
  HolyFramework.

If an operation is missing from the HolyFramework public API, extend
HolyFramework with a reusable primitive and consume that new API from the
module.

## Important limitation

The HolyFramework Module Exception can grant permissions only for copyright
owned or controlled by HolyFramework's copyright holder(s). It cannot waive
or alter third-party licenses.

A module that independently uses third-party GPL or other restricted code
must comply with those terms regardless of HolyFramework.

This document is a project licensing guide, not legal advice.

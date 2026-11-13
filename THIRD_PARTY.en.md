# Third-party components

[中文](THIRD_PARTY.md)

Third-party components this plugin links at runtime, with their licenses.

| Component | Version | License | Purpose |
|---|---|---|---|
| Qt 6 | 6.12.0 (host-shared) | LGPL-3.0 | Loaded through WinShun's shared Qt contract (`runtime.qt: "host"`); the package contains no Qt DLLs, they are deployed by the host. See [Qt licensing](https://www.qt.io/licensing/). |
| Microsoft Windows | WinGet v1.29.380 | System component | Invoked as an external subprocess (`winget.exe`); not bundled, not embedded. |
| Scoop | locally installed | MIT (Unlicense) | Invoked as an external PowerShell script (`scoop.ps1`); not bundled, not embedded. |
| Cascadia Mono / Microsoft YaHei UI | system fonts | System components | Referenced by `font.family`; no font files are distributed. |

## Build-time only (not distributed)

| Component | Purpose |
|---|---|
| CMake 3.21+ / Ninja | build system |
| MSVC 14.51 (VS 18) | compiler |
| Python 3.12 (tests only) | `tests/validate.py`, packaging scripts |

## No third-party source embedded

The plugin source contains no third-party source file, modified or verbatim; every component above is used as installed by the system or deployed by the host.

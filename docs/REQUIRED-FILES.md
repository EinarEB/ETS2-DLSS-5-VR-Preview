# Required files and game compatibility

## Game compatibility

The current installer accepts **ETS2 VR 1.60.1.1007** on Steam's **oculus** branch and checks the executable's exact fingerprint. A newer Steam update may need a matching preview release. If setup reports a different game version, stop there and check for an updated preview.

**OpenXR and DirectX 11 are required.** The launcher selects DirectX 11 and OpenXR; the renderer processes the D3D11 eye images. Virtual Desktop users should select VDXR.

## File fingerprints

Setup checks file contents against the SHA-256 fingerprints below. Matching filenames or version labels alone are not enough.

| Installed component | File version | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| ETS2 VR `eurotrucks2.exe` | 1.60.1.1007 | 51,881,872 | `03a0a051ecaddf95a4271e014e13c15ddddc2381c094278672a84d37206aa49f` |
| ReShade `ReShade64.dll` | 6.8.0.2155 | 5,592,064 | `0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7` |
| Snowymoon ZIP's `dxgi.dll` | Lighting 2.5.7 | 18,477,152 | `1bc3e4d8c270ce3843f131d16e42aa39c03f7ff10634ca5a2a7db3a3d28fcacb` |
| `renodx-dlss5.addon64` | 0.2026.827.2036 | 391,168 | `87aef9ddd937c7241e6bf8d8efea0045d63559135e254c60dab316db3d3a4aee` |
| `nvngx_dlss.dll` | 310.8.0.0 | 58,956,400 | `c85f971ce023c9f3492fc7455f0b01a24ba18ea39636407a846902c4360b0b7e` |
| `nvngx_dlssnr.dll` | 310.8.0.0 | 165,840,496 | `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e` |

## Download help

- **ReShade:** use the [official 6.8.0 full add-on installer](https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe). Setup reads it as data; you do not need to run it separately.
- **Snowymoon:** use Lighting **2.5.7** from [the author](https://snowymoon.io/) with your own subscription. Keep the ZIP intact. Activate in game when prompted.
- **Classic add-on:** download the standalone **renodx-dlss5.addon64** from [this release](https://github.com/yumlevi/renodx-dlss-installer/releases/tag/latest), version **0.2026.827.2036**. The `-v2.5` file and newer ShortFuse tools are different builds.
- **Models:** find **DLSS310.8.0-Streamline2.13.zip** in [RenoDX Discord](https://discord.com/invite/renodx) → **dlss5-downloads**. Extract both listed NVIDIA DLLs. Attachment links can expire; obtain a fresh link from the same post. Do not substitute `streamline.zip` or an older-RTX patch.

If a file is unavailable or rejected, stop and report the exact message. Renaming a different file does not make it compatible.

## System checks

Setup requires Windows 11 x64, an RTX 50-series primary adapter with 16 GB or more physical VRAM, driver 616.64 or newer, the installed NVIDIA NGX component, a valid x64 OpenXR runtime, the Visual C++ x64 runtime and 8 GB free on the same NTFS drive as ETS2. Checks use local files and hardware information; setup does not download or run third-party installers.

The VRAM check uses dedicated memory reported by DXGI, allowing for up to 1 GB reserved by the driver on a 16 GB card. Shared system RAM does not count. The launcher repeats the hardware/runtime checks and verifies the prepared files and game archives before starting. DirectX 11 and OpenXR are selected automatically.

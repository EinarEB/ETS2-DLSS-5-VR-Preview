# Exact required files

Setup validates complete SHA-256 fingerprints, not just filenames or version labels. These fingerprints identify the private baseline and do not grant permission to distribute someone else's files.

| Installed component | File version | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| ETS2 VR `eurotrucks2.exe` | 1.60.1.1007 | 51,881,872 | `03a0a051ecaddf95a4271e014e13c15ddddc2381c094278672a84d37206aa49f` |
| ReShade `ReShade64.dll` | 6.8.0.2155 | 5,592,064 | `0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7` |
| Snowymoon ZIP's `dxgi.dll` | Lighting 2.5.7 | 18,477,152 | `1bc3e4d8c270ce3843f131d16e42aa39c03f7ff10634ca5a2a7db3a3d28fcacb` |
| `renodx-dlss5.addon64` | 0.2026.827.2036 | 391,168 | `87aef9ddd937c7241e6bf8d8efea0045d63559135e254c60dab316db3d3a4aee` |
| `nvngx_dlss.dll` | 310.8.0.0 | 58,956,400 | `c85f971ce023c9f3492fc7455f0b01a24ba18ea39636407a846902c4360b0b7e` |
| `nvngx_dlssnr.dll` | 310.8.0.0 | 165,840,496 | `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e` |

## Acquisition evidence

**ReShade:** the [official full add-on installer](https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe) contains the tested runtime. Leave it as an EXE in Required files. Setup reads the embedded ZIP; it does not execute that installer. An already extracted exact `ReShade64.dll` is also accepted.

**Snowymoon:** obtain the [2.5.7 ZIP](https://cdn.snowymoondl.net/lighting_v2/ets2ats_lighting_v2_5_7_snowymoon.io.zip) through the [author](https://snowymoon.io/), using your own access. Setup extracts only `dxgi.dll`. The ZIP's other optional DLSS file is not used; the preview requires the separately listed 310.8 model. Do not copy subscriber account files into the release or Required files folder.

**Classic consumer:** the original tested download came from [yumlevi's community installer release](https://github.com/yumlevi/renodx-dlss-installer/releases/tag/latest). On September 6, GitHub's release metadata still listed `renodx-dlss5.addon64`, asset **534399348**, with the exact fingerprint above. The separate `renodx-dlss5-v2.5.addon64` asset has the same size but a different hash. Download only the standalone matching add-on; running another installer is unnecessary. This establishes acquisition provenance and current metadata, not a release-specific redistribution grant from the consumer's original author. The preview does not repackage it.

**310.8 models:** the tested `DLSS310.8.0-Streamline2.13.zip` is 187,288,978 bytes. Its two model entries match the table exactly. The retained download record identifies Discord channel **1545049227321810974**, attachment **1545050050609025114**. The project owner supplied a [working download link](https://cdn.discordapp.com/attachments/1545049227321810974/1545050050609025114/DLSS310.8.0-Streamline2.13.zip?ex=6a9eaffd&is=6a9d5e7d&hm=9497e1aa86dccdbc5116dbffad5000dc699a89f9c71636797082f8678617d31d&); its headers returned HTTP 200 and the expected archive length on September 6. This link expires **7 September 2026, 12:37 UTC**. A durable original message link is still needed. Search for the exact archive name in the RenoDX community's dlss5-downloads channel; the filename alone does not replace the setup hash check. After expiry, open the community and obtain a refreshed attachment link from that archive post. The offline guide changes its download button to the Discord fallback after the stated expiry.

The current newer ShortFuse add-on, Krish V4.7, patched older-RTX model and official SDK's differently versioned model are not tested substitutes. If the listed downloads are unavailable, report that acquisition problem instead of bypassing the installer checks.

A [September 4 mod-author guide](https://www.nexusmods.com/riseofthetombraider/mods/288) independently names RenoDX → dlss5-downloads → this exact archive. This is a community acquisition route, not a permanent direct download. Do not substitute the adjacent `streamline.zip` on the Classic add-on release page: its [issue report](https://github.com/yumlevi/renodx-dlss-installer/issues/1) identifies a different neural-model fingerprint.

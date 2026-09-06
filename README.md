# ETS2 DLSS 5 VR Preview

[![View interactive comparisons — cab and exterior before/after slider](docs/images/comparisons.svg)](https://einareb.github.io/ETS2-DLSS-5-VR-Preview/)

### [Choose your settings and compare before / after →](https://einareb.github.io/ETS2-DLSS-5-VR-Preview/)

DLSS 5 neural rendering for **Euro Truck Simulator 2 in VR**, with Snowymoon lighting and four quality presets.

### [Download v0.1 for Windows →](https://github.com/EinarEB/ETS2-DLSS-5-VR-Preview/releases/download/v0.1/ETS2-DLSS-5-VR-Preview-0.1-win-x64.zip)

Experimental preview. Flicker and image distortion remain possible.

## Requirements

- **RTX 50-series GPU with at least 16 GB VRAM**, and NVIDIA driver **616.64 or newer**. Use the latest driver available for your card.
- **Windows 11, 64-bit.**
- **ETS2 VR 1.60.1.1007** (`oculus` in Steam → Properties → Betas), **DirectX 11 and OpenXR**. Setup rejects other game builds; [compatibility details](docs/REQUIRED-FILES.md#game-compatibility).
- **Snowymoon Lighting access**, an OpenXR-compatible headset and **8 GB free** on your ETS2 drive.

## Install

### 1. Download the required files

Extract the [preview release ZIP](https://github.com/EinarEB/ETS2-DLSS-5-VR-Preview/releases/download/v0.1/ETS2-DLSS-5-VR-Preview-0.1-win-x64.zip), then put these files in its **Required files** folder:

| Download | What to put in the folder |
| --- | --- |
| [ReShade 6.8.0 — full add-on support](https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe) | `ReShade_Setup_6.8.0_Addon.exe` — leave it intact. |
| [Snowymoon Lighting 2.5.7](https://cdn.snowymoondl.net/lighting_v2/ets2ats_lighting_v2_5_7_snowymoon.io.zip) | `ets2ats_lighting_v2_5_7_snowymoon.io.zip` — leave it zipped. |
| [Krish Classic add-on](https://github.com/yumlevi/renodx-dlss-installer/releases/tag/latest) | `renodx-dlss5.addon64`, version **0.2026.827.2036**. Choose the standalone file without `-v2.5` in its name. |
| [DLSS 310.8 model archive](https://cdn.discordapp.com/attachments/1545049227321810974/1545050050609025114/DLSS310.8.0-Streamline2.13.zip?ex=6a9eaffd&is=6a9d5e7d&hm=9497e1aa86dccdbc5116dbffad5000dc699a89f9c71636797082f8678617d31d&) | Extract **both** `nvngx_dlss.dll` and `nvngx_dlssnr.dll`. |

These third-party files are supplied separately. If the model link expires, find **DLSS310.8.0-Streamline2.13.zip** in [RenoDX Discord](https://discord.com/invite/renodx) → **dlss5-downloads**; [download help and exact versions](docs/REQUIRED-FILES.md) are here if a link or file fails.

### 2. Run setup

Open **Setup ETS2 VR Preview.exe** → **Check requirements** → **Prepare preview**. Use the suggested new folder on the same drive as ETS2 and leave **Create a desktop shortcut** selected.

Setup checks your PC, runtime, downloads and install location before enabling installation.

The preview has its own settings and saves. Your normal game and campaigns stay in place. Keep the prepared folder where setup creates it.

### 3. Start VR

Connect your headset and keep Steam open. For Virtual Desktop, select **VDXR**. Open the **ETS2 DLSS 5 VR Preview** desktop shortcut and select **Start VR**.

On first launch, create a **new local profile with Steam Cloud unchecked**, set up your wheel or controller, and activate Snowymoon if prompted (**End** opens its menu). Enter the truck and let initialization finish.

Start with **Medium · Natural · intensity 2 · Cooler**.

## Adjust and compare

| Quality | Use it for |
| --- | --- |
| Low | More performance; one pass over a smaller area. |
| **Medium** | **The recommended starting point.** One pass with more coverage and detail. |
| High | Two passes at reduced resolution. |
| Ultra | Two passes over the full image at full resolution. |

**Changing quality requires closing and restarting ETS2.** Set style, intensity and model in the launcher before starting; live controls are also available in ReShade's Add-ons tab.

- **Scroll Lock:** compare the original image and the full neural effect. Processing stays active during comparison.
- **Home:** open ReShade. Use **Add-ons → DLSS 5 Feed** for VR color, blend and captures.
- **End:** open Snowymoon.

Flicker and image distortion can still occur. If motion looks unstable, try **Low** or lower your headset resolution.

[Settings and troubleshooting](docs/USAGE.md) · [Report a problem](https://github.com/EinarEB/ETS2-DLSS-5-VR-Preview/issues) · [Build from source](docs/BUILD.md)

## Credits

Built on DLSS5-Feeder, NIGos' bridge, ReShade, Vort and prod80. Includes components licensed for noncommercial use; see [credits and licenses](THIRD-PARTY-NOTICES.md).

# Settings and troubleshooting

[Back to setup](../README.md#install)

## Quality

| Preset | Passes per eye | Input resolution | Centered square |
| --- | ---: | ---: | ---: |
| Low | 1 | 50% | 60% |
| **Medium** | **1** | **65%** | **75%** |
| High | 2 | 80% | 90% |
| Ultra | 2 | 100% | Off: whole eye |

Resolution scales the selected area. Square size is a percentage of the shorter eye dimension; the edge blends into the original image. A 100% square can still crop a rectangular eye. Turn cropping off for the whole image.

**Quality, crop size, resolution and pass count require a full game restart.** The add-on shows saved and active settings separately. The crop stays centered in each eye; it does not follow your gaze.

## Appearance

Set **style, intensity and model preset** in the launcher while the game is closed. Start with **Natural, intensity 2, model Preset 1**. The neural add-on also exposes live controls in ReShade.

- **Style:** Natural, Default or Cinematic changes the look.
- **Intensity:** controls the strength of the neural changes.
- **Model preset:** Default or Preset 1–3 requests a model preset from the Classic add-on. It is separate from quality and style; a different request may not change the model used by the supplied files.
- **VR color look:** optional grading after neural rendering. Change it in **Home → Add-ons → DLSS 5 Feed**. The Home tab's preset selector on the monitor controls desktop shaders.
- **Final effect blend:** mixes the original image and neural output. It can change live.

**Scroll Lock** temporarily switches the neural blend between 0% and 100%. Under **More options → Compare key**, choose Pause instead or disable the shortcut. Neural processing continues and color grading remains applied, so the key compares appearance, not performance. Moving the blend slider ends the temporary comparison.

## Troubleshooting

| Problem | Try this |
| --- | --- |
| Waiting for NGX / no effect | Enter a driving scene and check **Home → Add-ons**. Restart if requested. Use only the supplied configuration and the matching Classic consumer. |
| Low FPS, flicker or unstable detail | Select Low or Medium and restart. Lower headset resolution or game settings. Start with game scaling at 100%. |
| Missing wheel or controller settings | Run the game's input wizard in your new local profile. Per-profile bindings are not imported. |
| Setup rejects a file or game version | Check [required files and game compatibility](REQUIRED-FILES.md). |
| Missing Visual C++ runtime | Install the [Microsoft Visual C++ x64 runtime](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist), then rerun setup. |
| Steam updated ETS2 | Prepare a new preview with a release that supports the updated game. |
| Monitor picture has the wrong proportions | Include a Windows screenshot in your report. The monitor correction is still experimental; a ReShade capture is taken before that correction. |

Start with the preview's supplied graphics configuration. Extra graphics mods and post-processing can change the result.

## Captures and reports

Use **Capture comparison** in the launcher or **Home → Add-ons → DLSS 5 Feed → Record screenshots or motion**. Comparison captures save four stereo frames before color grading. **Save final VR image** includes the grading. Recording can briefly pause rendering and needs several GB of free space.

For a bug report, choose **More options → Save diagnostics** and attach `preview-diagnostics.json` to a GitHub issue with your GPU, driver, headset, runtime, preset and a short description. Review optional logs and images before sharing; they may contain personal paths or scene information.

## Return to your usual game

Close ETS2 and let the launcher finish its settings check, then launch normally through Steam.

To uninstall, keep any preview saves or captures you want, then delete the separate preview folder and shortcut. Do not edit the shared `.scs` game archives in place.

[Technical notes](PIPELINE.md) · [Validation records](VALIDATION.md)

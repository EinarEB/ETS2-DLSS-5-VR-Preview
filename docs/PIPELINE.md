# Rendering and settings contract

The supported path is ETS2 Direct3D 11 → OpenXR → ReShade's windowless stereo runtime. A desktop effect runtime is never accepted as the neural image source, regardless of configuration. The classic neural consumer is the only supported consumer in this preview.

1. The depth companion tracks the render lineage and accepts a matched depth pair for the two eye images. Missing or invalid depth prevents processing; it is not replaced with a guessed constant.
2. The Vort adaptation estimates each eye's motion independently. Textures and history remain separated at the eye boundary. The final coordinate conversion into the Feed convention occurs once.
3. The Feed stage receives the combined stereo image and guides. It first resamples the full stereo image and guides to the selected work resolution, converting motion to work pixels. It then copies a centered square from each work eye when cropping is enabled. Cropping itself adds no vector scaling and does not reduce the earlier full-image guide/resampling workload.
4. Private D3D12 resources and fences transport the input from D3D11. Each eye/pass has a separate feature slot. The classic consumer intercepts the carrier evaluations and applies neural rendering.
5. The preview preserves the game image as its detail source. It expands and composites the neural difference over it, with a soft edge around a cropped region. Floating-point color remains available between passes.
6. The optional temporal filter stabilizes the neural difference using depth and estimated motion. It does not replace the original image with an accumulated low-resolution frame.
7. The selected blend is latched once per stereo frame. Both eyes, profiling and comparison captures use that same value. A delivered-frame acknowledgement is published only after the output reaches the VR image.
8. Optional prod80 grading runs after the neural composite. Final VR screenshots include it; comparison packets expose the earlier inputs and result.

The crop is a fixed centered region. It is not gaze tracking, variable-rate shading or a mask applied only after full-resolution neural work. Its performance benefit comes from smaller neural inputs. The full eye continues to be rendered by ETS2.

## Driver lifetime

The preview loads the installed NVIDIA driver's registered `_nvngx.dll` through the MIT bridge interface. It links no NVIDIA SDK implementation and packages no driver or model DLL. The protocol is not a stable public compatibility promise; the exact tested driver matters.

The direct-driver path uses one D3D12/NGX session per process. Fatal initialization, device loss or teardown latches a restart requirement. After its frame resources have retired, the owned device and global parameter block are retained for process cleanup; the add-on does not call the observed-crashing direct Shutdown1 route. If GPU work cannot retire, dependent resources are retained too. The session is never silently recreated on a new device.

This is an explicit stability tradeoff, not a claim of a general reusable driver lifecycle. Closing the game releases the process resources. Initialization, normal exit and relaunch remain part of ongoing headset validation.

At Windows process termination, the three add-ons skip their DLL-detach cleanup. Their GPU-owning registries do not run automatic COM destructors then; ordinary runtime/device callbacks still perform their explicit cleanup. This avoids queue waits and driver calls under the loader lock after other threads may have stopped. It does not control shutdown code in third-party DLLs. See Microsoft's [DllMain termination guidance](https://learn.microsoft.com/en-us/windows/win32/dlls/dllmain).

## Settings and comparison

The launcher edits settings only while ETS2 is closed. Each control writes only its relevant keys, makes backups and uses atomic file replacement. Appearance changes spanning two ReShade files restore the already-written file if a later write fails.

The keyboard comparison is a temporary session override. Key handling requires the game to be in the foreground, and a release must precede each press. Config reloads cannot undo that override; a manual blend change can. At 0%, neural work and temporal history continue, so toggling it does not measure the cost of disabling neural rendering.

`preview-status.json` includes process identity, a runtime generation and selected/delivered serials. The launcher rejects stale, oversized or foreign-session status. A successful NGX result and a delivered-frame acknowledgement are separate observations; neither proves good image quality or comfortable binocular viewing.

## Isolation and files

Setup creates a new directory on the same NTFS drive as the installed game. Large `.scs` archives are hard links; native executable/DLL files are independent copies. The launcher checks the source executable hash and archive size/timestamp before launch so a Steam update stops the old preview.

ReShade, Snowymoon and the model/consumer files come only from user-selected inputs with exact fingerprints. The local OpenXR layer is enabled for the launched child process. Setup does not replace the global OpenXR runtime, install a system service or modify Steam launch options.

The separate game home begins with global graphics settings and optional global input declarations copied from the user's normal Documents folder. Campaigns, per-profile controls, Steam Cloud data, Snowymoon credentials and graphics mods are not copied. New local profiles must be created in the game.

The installer journals only files and directories it creates. Cancellation or failure removes those exact files without recursive traversal; a cleanup failure retains an incomplete marker. An incomplete installation cannot launch.

## Monitor projection

The monitor guard acts only on the native D3D11 desktop swapchain for the `prism3d` window. It uses canonical COM identity and never casts the OpenXR runtime to a DirectX swapchain. The temporary full-backbuffer viewport and scissor are restored after downstream presentation.

The correction requires a protected immediate context and the presenting thread to own the desktop window. A different window thread makes the guard pass through and log why: holding the context lock while DXGI waits for another window thread can form a lock cycle. This conservative gate reduces compatibility. Its final visible result still needs a Windows monitor capture; an earlier ReShade screenshot is insufficient evidence.

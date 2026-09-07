@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 call :load_visual_studio
if errorlevel 1 exit /b 1
if not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
  echo Run this from an x64 Native Tools Command Prompt for VS 2022.
  exit /b 1
)
for %%d in (feeder depth monitor ui tests\blend tests\depth tests\setup tests\launcher) do if not exist "build\%%d" mkdir "build\%%d"
> build\toolchain.txt echo MSVC=%VCToolsVersion%
>> build\toolchain.txt echo WindowsSDK=%WindowsSDKVersion%
>> build\toolchain.txt echo Target=%VSCMD_ARG_TGT_ARCH%
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "HOOK=external\minhook\src\buffer.c external\minhook\src\hook.c external\minhook\src\trampoline.c external\minhook\src\hde\hde64.c"
rc /nologo /fo build\feeder\version.res src\feeder\version.rc || exit /b 1
cl /nologo /LD /EHsc /O2 /MD /W3 /std:c++20 /Iexternal\reshade\include /Iexternal\vulkan /Iexternal\imgui /Iexternal\minhook\include /Fobuild\feeder\ /Fdbuild\feeder\ src\feeder\dlss5-feed.cpp %HOOK% /link /OUT:build\feeder\dlss5-feed.addon64 /IMPLIB:build\feeder\dlss5-feed.lib build\feeder\version.res version.lib kernel32.lib user32.lib advapi32.lib ole32.lib || exit /b 1
cl /nologo /LD /EHsc /O2 /MD /W4 /WX /std:c++20 /DDEPTH_MATCH_MAX_CANDIDATES=4 /Iexternal\reshade\include /Fobuild\depth\ /Fdbuild\depth\ src\depth\depth_match_addon.cpp /link /OUT:build\depth\ets2-stereo-depth.addon64 /IMPLIB:build\depth\ets2-stereo-depth.lib d3d11.lib d3dcompiler.lib dxgi.lib bcrypt.lib || exit /b 1
cl /nologo /LD /EHsc /O2 /MD /W3 /std:c++17 /Iexternal\reshade\include /Iexternal\minhook\include /Fobuild\monitor\ /Fdbuild\monitor\ src\monitor\monitor_present.cpp %HOOK% /link /OUT:build\monitor\ets2-monitor.addon64 /IMPLIB:build\monitor\ets2-monitor.lib user32.lib || exit /b 1
"%CSC%" /nologo /langversion:5 /target:winexe /platform:x64 /optimize+ /win32icon:assets\preview.ico /win32manifest:assets\application.manifest /out:"build\ui\Setup ETS2 VR Preview.exe" /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll src\launcher\AssemblyInfo.cs src\launcher\PreviewForm.cs src\launcher\PreviewSetup.cs src\launcher\SetupEngine.cs src\launcher\PreviewFiles.cs src\launcher\Requirements.cs src\launcher\DesktopShortcut.cs || exit /b 1
"%CSC%" /nologo /langversion:5 /target:winexe /platform:x64 /optimize+ /win32icon:assets\preview.ico /win32manifest:assets\application.manifest /out:"build\ui\ETS2 VR Preview.exe" /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll src\launcher\PreviewLauncher.cs src\launcher\LauncherOptions.cs src\launcher\PreviewFiles.cs src\launcher\Requirements.cs src\launcher\AssemblyInfo.cs src\launcher\PreviewForm.cs || exit /b 1
cl /nologo /EHsc /O2 /MD /W3 /std:c++20 tests\preview_blend_test.cpp /Fobuild\tests\blend\ /Febuild\tests\blend\preview_blend_test.exe || exit /b 1
cl /nologo /EHsc /O2 /MD /W3 /std:c++20 /DDEPTH_MATCH_MAX_CANDIDATES=4 tests\depth_match_gpu_test.cpp /Fobuild\tests\depth\ /Febuild\tests\depth\depth_match_gpu_test.exe d3d11.lib d3dcompiler.lib || exit /b 1
"%CSC%" /nologo /langversion:5 /target:exe /platform:x64 /optimize+ /out:build\tests\setup\SetupTests.exe /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll tests\SetupTests.cs src\launcher\SetupEngine.cs src\launcher\PreviewFiles.cs src\launcher\Requirements.cs || exit /b 1
"%CSC%" /nologo /langversion:5 /target:exe /platform:x64 /optimize+ /main:ReleaseLauncherTests /out:build\tests\launcher\ReleaseLauncherTests.exe /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll tests\ReleaseLauncherTests.cs src\launcher\PreviewLauncher.cs src\launcher\LauncherOptions.cs src\launcher\PreviewFiles.cs src\launcher\Requirements.cs src\launcher\AssemblyInfo.cs src\launcher\PreviewForm.cs || exit /b 1
"%CSC%" /nologo /langversion:5 /target:exe /platform:x64 /optimize+ /out:build\tests\setup\RequirementTests.exe /r:System.Web.Extensions.dll tests\RequirementTests.cs src\launcher\Requirements.cs src\launcher\PreviewFiles.cs || exit /b 1
call test.cmd
exit /b %errorlevel%

:load_visual_studio
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  echo Install Visual Studio 2022 Build Tools with Desktop development with C++.
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PREVIEW_VS=%%i"
if not defined PREVIEW_VS exit /b 1
call "%PREVIEW_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%

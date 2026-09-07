@echo off
setlocal
cd /d "%~dp0"
rem Offline fixture feeder: the release source plus the sequence/packet replay
rem inputs (ETS2_REPLAY_SEQUENCE / ETS2_REPLAY_PACKET). Never ship this binary;
rem it substitutes recorded inputs for the game's own. Used by tools/replay_branch.py.
where cl >nul 2>nul
if errorlevel 1 call :load_visual_studio
if errorlevel 1 exit /b 1
if not exist build\replay mkdir build\replay
set "HOOK=external\minhook\src\buffer.c external\minhook\src\hook.c external\minhook\src\trampoline.c external\minhook\src\hde\hde64.c"
rc /nologo /fo build\replay\version.res src\feeder\version.rc || exit /b 1
cl /nologo /LD /EHsc /O2 /MD /W3 /std:c++20 /DETS2_FEED_SEQUENCE_REPLAY /DETS2_FEED_PACKET_REPLAY /Iexternal\reshade\include /Iexternal\vulkan /Iexternal\imgui /Iexternal\minhook\include /Fobuild\replay\ /Fdbuild\replay\ src\feeder\dlss5-feed.cpp %HOOK% /link /OUT:build\replay\dlss5-feed.addon64 /IMPLIB:build\replay\dlss5-feed.lib build\replay\version.res version.lib kernel32.lib user32.lib advapi32.lib ole32.lib || exit /b 1
echo Replay feeder built: build\replay\dlss5-feed.addon64 (offline fixture only).
exit /b 0

:load_visual_studio
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  echo Install Visual Studio 2022 Build Tools with Desktop development with C++.
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PREVIEW_VS=%%i"
if not defined PREVIEW_VS exit /b 1
rem The developer command script may change the working directory when it is not
rem started from a developer prompt; pin it so the relative paths above still work.
set "VSCMD_START_DIR=%~dp0"
call "%PREVIEW_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
exit /b %errorlevel%

@echo off
setlocal
cd /d "%~dp0"
echo GPU check: stereo depth matcher proof, identity assignment and hold. Needs a D3D11 hardware device; not part of test.cmd.
build\tests\depth\depth_match_gpu_test.exe || exit /b 1
echo GPU check passed. No game, NGX or headset work ran.
exit /b 0

@echo off
setlocal
cd /d "%~dp0"
build\tests\blend\preview_blend_test.exe > build\tests\blend\results.json || exit /b 1
rem Use a short unique fixture path on the current NTFS drive. Files are tiny dummy data.
build\tests\setup\SetupTests.exe "%TEMP%\ets2-preview-tests" || exit /b 1
build\tests\launcher\ReleaseLauncherTests.exe || exit /b 1
echo CPU and file tests passed. No game, NGX or GPU tests ran.
exit /b 0

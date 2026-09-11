@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d E:\Projects\OES
set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
"%CMAKE%" build\windows-x64-release >nul 2>&1
"%CMAKE%" --build build\windows-x64-release --target oes_tests
echo BUILD_EXIT=%errorlevel%

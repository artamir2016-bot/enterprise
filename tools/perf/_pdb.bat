@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d E:\Projects\OES
set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
"%CMAKE%" -S . -B build\windows-x64-release -DCMAKE_CXX_FLAGS_RELEASE="/MD /O2 /Ob2 /DNDEBUG /Zi" -DCMAKE_SHARED_LINKER_FLAGS="/DEBUG" -DCMAKE_EXE_LINKER_FLAGS="/DEBUG"
echo CFG_EXIT=%errorlevel%
"%CMAKE%" --build build\windows-x64-release --target backend designer oes_config_gen
echo BUILD_EXIT=%errorlevel%

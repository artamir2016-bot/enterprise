@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d E:\Projects\OES
set "CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
"%CMAKE%" -S . -B build-perf -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DOES_USE_FIREBIRD=ON
"%CMAKE%" --build build-perf --target oes_bench
echo BUILD_EXIT=%errorlevel%

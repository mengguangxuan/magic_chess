@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
set "BUILD_DIR=%TEMP%\recognization_host_build"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
pushd "%BUILD_DIR%"
cl /nologo /TC /W4 /I"%~dp0..\src\tscp" /c "%~dp0..\src\tscp\board.c" "%~dp0..\src\tscp\data.c" "%~dp0..\src\tscp\eval.c" "%~dp0..\src\tscp\search.c"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /TP /std:c++17 /EHsc /W4 /I"%~dp0." /I"%~dp0.." /c "%~dp0..\engine_adapter.cpp" "%~dp0..\move_quality.cpp" "%~dp0arduino_stub.cpp" "%~dp0engine_smoke_test.cpp"
if errorlevel 1 exit /b %errorlevel%
link /nologo board.obj data.obj eval.obj search.obj engine_adapter.obj move_quality.obj arduino_stub.obj engine_smoke_test.obj /out:engine_smoke_test.exe
if errorlevel 1 exit /b %errorlevel%
engine_smoke_test.exe
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%

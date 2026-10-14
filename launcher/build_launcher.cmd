@echo off
rem Builds "1vs100 Launcher.exe" in the workspace root from work\launcher\launcher.cpp.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
rem clang-cl from LLVM: set LLVM_BIN to its bin folder, or have clang-cl on PATH.
if defined LLVM_BIN (set CLANG=%LLVM_BIN%\clang-cl.exe) else (set CLANG=clang-cl.exe)
set OUT=%~dp0..\..\1vs100 Launcher.exe
"%CLANG%" /nologo /std:c++20 /O2 /EHsc /W3 /DUNICODE /D_UNICODE "%~dp0launcher.cpp" /Fo"%TEMP%\onevs100_launcher.obj" /Fe"%OUT%" /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup
if errorlevel 1 exit /b %errorlevel%
echo Built "%OUT%"

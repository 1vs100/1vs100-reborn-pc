@echo off
rem Builds the Avatar Editor recompilation. See build_avatar_editor.ps1.
rem   build_avatar_editor.cmd          build (unpacks the source the first time)
rem   build_avatar_editor.cmd -Clean   start over from the source zip
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_avatar_editor.ps1" %*
exit /b %errorlevel%

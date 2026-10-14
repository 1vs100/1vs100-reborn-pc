@echo off
rem Copies your own Xbox 360 Avatar items into the Avatar Editor's closet
rem (the launcher's "Import Avatar items..." button does the same).
rem   Import-AvatarCloset.cmd                          asks for a folder
rem   Import-AvatarCloset.cmd -Source "D:\Xbox\Content"  a folder to search
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Import-AvatarCloset.ps1" %*
echo.
pause

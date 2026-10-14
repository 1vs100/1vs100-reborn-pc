@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%
if not defined STEAMWORKS_SDK (echo Set STEAMWORKS_SDK to your Steamworks SDK "sdk" folder.& exit /b 1)
pushd "%~dp0"
cl /nologo /std:c++17 /EHsc /W4 /I"%STEAMWORKS_SDK%\public" steam_transport_test.cpp /Fe:steam_transport_test.exe /link "%STEAMWORKS_SDK%\redistributable_bin\win64\steam_api64.lib"
if errorlevel 1 exit /b %errorlevel%
copy /y "%STEAMWORKS_SDK%\redistributable_bin\win64\steam_api64.dll" . >nul
echo 480>steam_appid.txt
cl /nologo /std:c++17 /EHsc /W4 room_authority_test.cpp /Fe:room_authority_test.exe
if errorlevel 1 exit /b %errorlevel%
room_authority_test.exe
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++20 /EHsc /W4 /I"%STEAMWORKS_SDK%\public" native_room_test.cpp "..\..\native\src\native_room.cpp" /Fe:native_room_test.exe /link "%STEAMWORKS_SDK%\redistributable_bin\win64\steam_api64.lib"
if errorlevel 1 exit /b %errorlevel%
popd

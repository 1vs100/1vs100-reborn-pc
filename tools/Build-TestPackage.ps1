# Builds the native game and the launcher, then packages a dated Private Alpha Test build:
#   dist\1vs100-private-alpha-<yyyyMMdd-HHmmss>\  and  dist\1vs100-private-alpha-<yyyyMMdd-HHmmss>.zip
# Run from anywhere:
#   powershell -ExecutionPolicy Bypass -File tools\Build-TestPackage.ps1
# The package leaves out your profile, Avatar, saved stats, logs and crash
# dumps. Only share it with someone entitled to the game files.
param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stamp=Get-Date -Format yyyyMMdd-HHmmss
$diag=Join-Path $root 'outputs\diagnostics'
New-Item -ItemType Directory -Force $diag | Out-Null

function RunBuild([string]$script, [string]$log) {
    $process=Start-Process -FilePath $script -RedirectStandardOutput $log -RedirectStandardError "$log.err" -Wait -PassThru -NoNewWindow
    if(Test-Path "$log.err") { Get-Content "$log.err" | Add-Content $log; Remove-Item "$log.err" }
    return $process.ExitCode
}

if(-not $SkipBuild) {
    # Keep the previous native build before replacing it.
    $exe=Join-Path $root 'work\native-avatar-build\onevs100.exe'
    if(Test-Path $exe) {
        $backup=Join-Path $root "claude\backup\native-exe-$stamp"
        New-Item -ItemType Directory -Force $backup | Out-Null
        Copy-Item $exe $backup
        Write-Output "Previous native build saved to $backup"
    }
    $nativeLog=Join-Path $diag "build-native-$stamp.log"
    Write-Output "Building the native game (log: $nativeLog) ..."
    $code=RunBuild (Join-Path $root 'work\build_native_avatar.cmd') $nativeLog
    if($code -ne 0) { Get-Content $nativeLog -Tail 40; throw "Native build failed (exit $code). See $nativeLog" }
    Write-Output 'Native build OK.'
    $launcherLog=Join-Path $diag "build-launcher-$stamp.log"
    Write-Output "Building the launcher (log: $launcherLog) ..."
    $code=RunBuild (Join-Path $root 'work\launcher\build_launcher.cmd') $launcherLog
    if($code -ne 0) { Get-Content $launcherLog -Tail 40; throw "Launcher build failed (exit $code). See $launcherLog" }
    Write-Output 'Launcher build OK.'
}

# Every Lua file must keep CRLF line endings.
$stagedLua=Join-Path $root 'work\native-parity\assets\lua'
if (Test-Path $stagedLua) {
    $bad=@(Get-ChildItem $stagedLua -Filter *.lua | Where-Object {
        $text=[IO.File]::ReadAllText($_.FullName); ($text -replace "`r`n",'') -match "`n" })
    if($bad.Count) { throw ('Lua files with LF line endings: ' + ($bad.Name -join ', ')) }
}

$name="1vs100-private-alpha-$stamp"
$out=Join-Path $root "dist\$name"
New-Item -ItemType Directory -Force $out | Out-Null
function Put([string]$relative) {
    $source=Join-Path $root $relative
    $target=Join-Path $out $relative
    New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
    Copy-Item $source $target -Recurse -Force
}
Put '1vs100 Launcher.exe'
Put 'outputs\online-tools\Launch-NativeRoomTest.ps1'
Put 'outputs\online-tools\XzpMods.ps1'
Put 'outputs\online-tools\Export-GameContent.ps1'
Put 'outputs\online-tools\Collect-Logs.ps1'
Put 'outputs\online-tools\mods-README.txt'
Put 'work\native-parity\mods'
Put 'work\native-avatar-build\onevs100.exe'
Put 'work\native-parity\assets'
Put 'work\native-parity\content'
Put 'work\native-parity\rexruntime.dll'
Put 'work\native-parity\rexgpu-xenos.dll'
New-Item -ItemType Directory -Force (Join-Path $out 'outputs\diagnostics') | Out-Null
# The player's own mods folder (empty apart from its README).
New-Item -ItemType Directory -Force (Join-Path $out 'mods') | Out-Null
Copy-Item (Join-Path $root 'outputs\online-tools\mods-README.txt') (Join-Path $out 'mods\README.txt')
Set-Content (Join-Path $out 'launcher-settings.ini') @"
[launcher]
keyboard=1
input_backend=sdl
dualsense_direct=1
mouse_panel=0
questions=3 (quick test)
preshow_seconds=180
show_title=Back from the dead!
live_title=1 vs 100 LIVE
bots=1
bot_count=99
show_mode=Extended Play
gameserver_host=
sponsor=No sponsor (recommended)
show_skin=Standard
host_tool=Off
debug_input=0
fake_live_host=0
subtitles=0
sponsor_dance=0
fake_reals=0
no_mods=0
"@
Set-Content (Join-Path $out 'README.txt') @"
1 vs 100 (PC build) - Private Alpha Test, build $stamp
Please do not share this package outside the test group.

Setup
1. Unzip into a NEW folder and open "1vs100 Launcher.exe".
   To keep your profile and stats, copy your old player-data folder into it.
2. The first start asks for a gamertag. Picture: Play tab > "Edit profile".
3. For online play set online_service=gameserver and gameserver_host in
   onevs100.toml to your gameserver.

Avatars
- The game sends each player's own Avatar (body, face, colours
  and clothes) to the others through the gameserver; new in this build.
- Marketplace items a player has and you do not show as a similar standard
  item (a tee for a tee, glasses for glasses), or the default item for
  costumes, helmets and masks.

Controller (Controls tab)
- Plug the DualSense in by USB before starting. Click inside the game window.
- "DualSense (USB) direct backup" is on: the game also reads the pad itself.
- If the pad still does nothing: close DS4Windows if it is running.
- "Mouse button panel" adds a small window of clickable answer/menu buttons.
- Keyboard: Space = A, Backspace = B, L = X, P = Y, X = Start, W/S = up/down.

Debug tab (experimental, original development switches)
- Sponsor, show skin (Holiday, The Big Game, ...), the original Host Tool
  (LT/RT change pages, right stick click shows/hides it) and the debug menu
  (Back; left stick click = free camera). Applies to the next launch.
- "Export game images & text" puts every logo, image and on-screen text in
  mods\_export. See mods\README.txt to swap them. No prizes are awarded: the
  build replaces the original prize text with points-only text.

If anything goes wrong: launcher > Play > "Send logs" puts a zip of the newest
logs on your Desktop. Send it with what happened and roughly when.
"@
$zip=Join-Path $root "dist\$name.zip"
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip -CompressionLevel Optimal
$hash=(Get-FileHash $zip -Algorithm SHA256).Hash
Set-Content (Join-Path $diag "package-$stamp.txt") "Package: $zip`r`nBytes: $((Get-Item $zip).Length)`r`nSHA256: $hash"
Write-Output "Package: $zip"
Write-Output "SHA256: $hash"

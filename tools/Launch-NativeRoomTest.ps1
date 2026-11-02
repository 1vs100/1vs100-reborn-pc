# Modes: Offline (solo test staging), LocalHost + LocalJoin (two staged
# copies on this PC), Gameserver (staged copy for community-server play with
# stock game files; no Lua is patched).
param([ValidateSet('Offline','LocalHost','LocalJoin','Gameserver')][string]$Mode='Offline',
      [string]$ShowTitle='',[string]$LiveTitle='',[switch]$NoBots,[string]$GameArgs='',
      [ValidateRange(0,37)][int]$Questions=2,[ValidateRange(10,240)][int]$PreShowSeconds=30,
      [switch]$NoMenuBlur,[switch]$DualSenseDirect,[switch]$MousePanel,
      [ValidateSet('ExtendedPlay','Live')][string]$ShowMode='ExtendedPlay',
      [ValidateRange(1,10000)][int]$BotCount=99,
      # Launcher Debug tab (original development switches; see NativeDebug.lua).
      [ValidateSet('Off','Sprint')][string]$Sponsor='Off',
      [ValidateSet('Standard','Holiday','TheBigGame','XboxLIVE','ColinCorp','Sprint')][string]$Skin='Standard',
      [ValidateSet('Off','Host','Producer')][string]$HostTool='Off',
      [switch]$DebugInput,[switch]$FakeLiveHost,[switch]$Subtitles,[switch]$SponsorDance,[switch]$FakeReals,
      [switch]$NoMods)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$stage=Join-Path $root ('work/native-room-'+$Mode.ToLower())
$reference=Join-Path $root 'work/native-parity'
New-Item -ItemType Directory -Force $stage | Out-Null
$exe=Join-Path $stage 'onevs100-room.exe'
foreach($process in Get-Process | Where-Object { $_.Path -eq $exe }) { Stop-Process -Id $process.Id }
Copy-Item (Join-Path $root 'work/native-avatar-build/onevs100.exe') $exe
foreach($dll in @('rexruntime.dll','rexgpu-xenos.dll')) { Copy-Item (Join-Path $reference $dll) $stage }
Copy-Item (Join-Path $reference 'assets') $stage -Recurse -Force
# Mods: text and image replacements inside the staged UI packages (the reference
# copies stay original). Shipped mods (prize-free text, points glyph) first, then
# the player's own mods folder. -NoMods plays the original packages.
if(-not $NoMods) {
    . (Join-Path $PSScriptRoot 'XzpMods.ps1')
    $modRoots=@((Join-Path $reference 'mods'),(Join-Path $root 'mods'))
    foreach($package in @(@('launch','assets/media/launch.xzp'),@('episode','assets/media/episode.xzp'),@('episode','assets/Episode/media/episode.xzp'))) {
        $packagePath=Join-Path $stage $package[1]
        if(-not(Test-Path $packagePath)) { continue }
        try {
            $result=Invoke-XzpMods $packagePath $package[0] $modRoots
            Write-Output "Mods: $($package[1]): $($result.FilesReplaced) files, $($result.StringsChanged) strings"
            foreach($note in $result.Notes) { Write-Output "Mods: $note" }
        } catch {
            # A broken mod must not stop the game: start again from the original
            # package with only the shipped mods, or leave it original.
            Write-Output "Mods: $($package[1]): your mods were skipped: $($_.Exception.Message)"
            Copy-Item (Join-Path $reference $package[1]) $packagePath -Force
            try {
                $result=Invoke-XzpMods $packagePath $package[0] @($modRoots[0])
                Write-Output "Mods: $($package[1]): shipped mods only: $($result.FilesReplaced) files, $($result.StringsChanged) strings"
            } catch {
                Copy-Item (Join-Path $reference $package[1]) $packagePath -Force
                Write-Output "Mods: $($package[1]) left original: $($_.Exception.Message)"
            }
        }
    }
}
$profiles=Join-Path $stage 'userdata/profiles'
New-Item -ItemType Directory -Force $profiles | Out-Null
$referenceProfiles=Join-Path $reference 'userdata/profiles'
if(-not(Test-Path (Join-Path $profiles 'local-name.txt')) -and (Test-Path $referenceProfiles)) {
    Copy-Item (Join-Path $referenceProfiles '*') $profiles
}
# The second copy on this PC gets its own name (its XUID is derived per folder).
if($Mode -eq 'LocalJoin') {
    $nameFile=Join-Path $profiles 'local-name.txt'
    $referenceName=Get-Content (Join-Path $reference 'userdata/profiles/local-name.txt') -ErrorAction SilentlyContinue | Select-Object -First 1
    $currentName=Get-Content $nameFile -ErrorAction SilentlyContinue | Select-Object -First 1
    if(-not $currentName -or $currentName -eq $referenceName) { Set-Content $nameFile 'Player2' }
}
# One profile, Avatar and saved stats (season score, streaks) for every mode on
# this PC: player-data\ in the workspace. The second test copy (LocalJoin) keeps
# its own folder and identity slot so it is a different player.
$playerData=if($Mode -eq 'LocalJoin'){ Join-Path $stage 'userdata' }else{ Join-Path $root 'player-data' }
$profileSlot=if($Mode -eq 'LocalJoin'){ '2' }else{ '' }
# A fresh PC without a profile gets the game's profile editor on first start.
New-Item -ItemType Directory -Force (Join-Path $playerData 'profiles') | Out-Null
if(-not(Test-Path (Join-Path $playerData 'profiles/local-name.txt')) -and (Test-Path $referenceProfiles)) {
    Copy-Item (Join-Path $referenceProfiles '*') (Join-Path $playerData 'profiles')
}
# Live (Primetime): players must be in the lobby before The One and the Mob are
# picked, or they wait as join-in-progress for a next round that never comes when
# nobody else can be The One. Loading takes ~35 s, so allow at least 2 minutes.
if($ShowMode -eq 'Live' -and $PreShowSeconds -lt 120) {
    Write-Output "Live show: countdown raised from $PreShowSeconds to 120 seconds so you are in the lobby before roles are picked."
    $PreShowSeconds=120
}
# Offline test shows patch session timing and launcher options into the
# staged Lua. Gameserver mode runs the stock files untouched.
if ($Mode -ne 'Gameserver') {
    $clock=[DateTime]::UtcNow
    $epoch=$clock.ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
    $start=$clock.AddSeconds($PreShowSeconds).ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
    $end=$clock.AddSeconds($PreShowSeconds).AddHours(1).ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
    function WriteLua([string]$path,[string]$text) {
        $text=($text -replace "`r`n","`n") -replace "`n","`r`n"
        [IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    }
    $path=Join-Path $stage 'assets/startup/startup.lua'
    $text=[IO.File]::ReadAllText($path) -replace "OFFLINE_CLOCK_EPOCH = '[^']+'", "OFFLINE_CLOCK_EPOCH = '$epoch'"
    WriteLua $path $text
    # Show mode: grid GameType 1 = Live (Primetime), 2 = Extended Play. In a room
    # the host's session (with its GameType) is the show every copy plays.
    $gameType=if($ShowMode -eq 'Live'){1}else{2}
    $path=Join-Path $stage 'assets/startup/startup.lua'
    WriteLua $path ([IO.File]::ReadAllText($path) -replace 'OFFLINE_SESSION_GAME_TYPE = \d', "OFFLINE_SESSION_GAME_TYPE = $gameType")
    $path=Join-Path $stage 'assets/lua/program.lua'
    $text=[IO.File]::ReadAllText($path) -replace "OFFLINE_SESSION_START='[^']+'", "OFFLINE_SESSION_START='$start'"
    $text=$text -replace "OFFLINE_SESSION_END='[^']+'", "OFFLINE_SESSION_END='$end'"
    WriteLua $path $text
    if($Questions -gt 0) {
        $path=Join-Path $stage 'assets/lua/eptimings.lua'
        WriteLua $path ([IO.File]::ReadAllText($path)+"`r`nEPTimingsTable.maxNumberOfQuestions=$Questions -- isolated short lifecycle test`r`n")
        # The original show's last question (37) carries the final x3 bonus, and the
        # LastQuestion jumbotron formats LOCALIZE("MULTIPLIER", EPBonusQuestions[last]).
        # A shortened show must move that bonus to its new last question, or the
        # multiplier is nil (locale.lua:81 FormatLocString crash) and scoring differs.
        $path=Join-Path $stage 'assets/lua/data.lua'
        WriteLua $path ([IO.File]::ReadAllText($path)+"`r`n-- isolated short lifecycle test: final-question bonus moves to question $Questions`r`ndo local bonus=GameData.EPBonusQuestions NATIVE_EP_BONUS_FULL={} for q,v in pairs(bonus) do NATIVE_EP_BONUS_FULL[q]=v end local final=bonus[37] for q in pairs(bonus) do if q>=$Questions then bonus[q]=nil end end bonus[$Questions]=final end`r`n")
    }
    # Launcher "Show" tab options (only the host's apply in a shared room).
    $path=Join-Path $stage 'assets/lua/program.lua'
    # Live mode uses its own marquee title when one is given.
    $title=((&{ if($ShowMode -eq 'Live' -and $LiveTitle){$LiveTitle}else{$ShowTitle} }) -replace '[^A-Za-z0-9 !?.,-]','')
    if($title.Length -gt 24) { $title=$title.Substring(0,24) }
    if($title) { WriteLua $path ([IO.File]::ReadAllText($path)+"`r`nNATIVE_SHOW_TITLE = `"$title`" -- launcher show title`r`n") }
    # Launcher Debug tab: switches read when the game's Lua loads (globals.lua runs
    # first; NativeDebug.lua applies the NATIVE_* ones).
    $skinNames=@{Standard='';Holiday='Holiday';TheBigGame='TheBigGame';XboxLIVE='XboxLIVE';ColinCorp='ColinCorp';Sprint='Sprint'}
    $debugLua="`r`n-- launcher Debug tab`r`n"
    $debugLua+="NATIVE_SPONSORSHIP = $(if($Sponsor -eq 'Sprint'){'true'}else{'false'})`r`n"
    if($skinNames[$Skin]) { $debugLua+="NATIVE_SHOW_SKIN = '$($skinNames[$Skin])'`r`n" }
    if($HostTool -ne 'Off') { $debugLua+="NATIVE_HOST_TOOL = '$($HostTool.ToLower())'`r`n" }
    if($DebugInput) { $debugLua+="NATIVE_DEBUG_INPUT = true`r`n" }
    if($SponsorDance) { $debugLua+="NATIVE_SPONSOR_DANCE = true`r`n" }
    if($FakeLiveHost) { $debugLua+="DEFAULT_FAKE_LIVE_HOST = true`r`n" }
    if($Subtitles) { $debugLua+="DEFAULT_SHOW_SUBTITLES = true`r`n" }
    if($FakeReals) { $debugLua+="CONTENT_USE_FAKEREALS = true`r`n" }
    # Show tab: offline players in an Extended Play show (Live keeps a 100-seat Mob).
    if($BotCount -ne 99) { $debugLua+="NATIVE_BOT_COUNT = $BotCount`r`n" }
    $path=Join-Path $stage 'assets/lua/globals.lua'
    WriteLua $path ([IO.File]::ReadAllText($path)+$debugLua)
    Write-Output "Bots: $(if($NoBots){'off'}else{$BotCount}) (Extended Play)"
    Write-Output "Debug: sponsor=$Sponsor skin=$Skin hostTool=$HostTool debugInput=$DebugInput fakeLiveHost=$FakeLiveHost subtitles=$Subtitles sponsorDance=$SponsorDance fakeReals=$FakeReals mods=$(-not $NoMods)"
    if($NoBots) {
        $path=Join-Path $stage 'assets/lua/statelobby.lua'
        WriteLua $path ([IO.File]::ReadAllText($path)+"`r`nNATIVE_DISABLE_BOTS = true -- launcher: no offline players`r`n")
    }
    if($NoMenuBlur) {
        # Diagnostic: is the black main-menu background the native blur pass?
        $path=Join-Path $stage 'assets/lua/menu.lua'
        WriteLua $path ([IO.File]::ReadAllText($path)+"`r`n-- isolated diagnostic: menu blur disabled`r`nEnableBlur=function() end DisableBlur=function() end`r`n")
    }
}
$log=Join-Path $root ('outputs/diagnostics/native-room-'+$Mode.ToLower()+'-'+(Get-Date -Format yyyyMMdd-HHmmss)+'.log')
$oldContent=[Environment]::GetEnvironmentVariable('ONEVS100_NATIVE_AVATAR_CONTENT','Process')
# Extra player-1 input sources in the game (native_controls.cpp).
$controlVars=@{ONEVS100_DUALSENSE_HID=$(if($DualSenseDirect){'1'}else{$null});ONEVS100_MOUSE_PANEL=$(if($MousePanel){'1'}else{$null})}
$oldControlVars=@{}
foreach($key in $controlVars.Keys) { $oldControlVars[$key]=[Environment]::GetEnvironmentVariable($key,'Process') }
$sdlHints=@{SDL_JOYSTICK_HIDAPI='1';SDL_JOYSTICK_HIDAPI_PS5='1';SDL_GAMECONTROLLER_IGNORE_DEVICES='';SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT='';SDL_HIDAPI_IGNORE_DEVICES=''}
$oldSdlHints=@{}
foreach($key in $sdlHints.Keys) { $oldSdlHints[$key]=[Environment]::GetEnvironmentVariable($key,'Process') }
try {
    [Environment]::SetEnvironmentVariable('ONEVS100_PLAYER_DATA',$playerData,'Process')
    [Environment]::SetEnvironmentVariable('ONEVS100_PROFILE_SLOT',$(if($profileSlot){$profileSlot}else{$null}),'Process')
    [Environment]::SetEnvironmentVariable('ONEVS100_NATIVE_AVATAR_CONTENT',(Join-Path $reference 'content'),'Process')
    # Launcher Display/Controls options arrive as runtime flags (--fullscreen, --vsync, ...).
    $gameFlags=@($GameArgs -split '\s+' | Where-Object { $_ -match '^--[a-z_]+(=[A-Za-z0-9._-]+)?$' })
    if(-not ($gameFlags -match '^--mnk_mode')) { $gameFlags+='--mnk_mode=true' }
    if(-not ($gameFlags -match '^--input_backend=')) { $gameFlags+='--input_backend=sdl' }
    if($gameFlags -contains '--input_backend=sdl') {
        foreach($key in $sdlHints.Keys) { [Environment]::SetEnvironmentVariable($key,$sdlHints[$key],'Process') }
        Write-Output 'Controller: SDL, HIDAPI/PS5 enabled; inherited device exclusion hints cleared for this child.'
    }
    foreach($key in $controlVars.Keys) { [Environment]::SetEnvironmentVariable($key,$controlVars[$key],'Process') }
    if($DualSenseDirect) { Write-Output 'Controller: USB DualSense direct backup on (player 1).' }
    if($MousePanel) { Write-Output 'Controller: mouse button panel on.' }
    $process=Start-Process -FilePath $exe -WorkingDirectory $stage -WindowStyle Normal -ArgumentList (@("--log_file=$log",'--log_max_file_size_mb=64')+$gameFlags) -PassThru
    $process.Id | Set-Content (Join-Path $root 'work/native-room-visible-pid.txt')
    $log | Set-Content (Join-Path $root 'work/native-room-visible-log.txt')
    $log | Set-Content (Join-Path $root ('work/native-room-'+$Mode.ToLower()+'-log.txt'))
    Write-Output "pid=$($process.Id) mode=$Mode show=$ShowMode questions=$Questions showStart=$start log=$log"
} finally {
    foreach($key in $sdlHints.Keys) { [Environment]::SetEnvironmentVariable($key,$oldSdlHints[$key],'Process') }
    foreach($key in $controlVars.Keys) { [Environment]::SetEnvironmentVariable($key,$oldControlVars[$key],'Process') }
    [Environment]::SetEnvironmentVariable('ONEVS100_PLAYER_DATA',$null,'Process')
    [Environment]::SetEnvironmentVariable('ONEVS100_PROFILE_SLOT',$null,'Process')
    [Environment]::SetEnvironmentVariable('ONEVS100_NATIVE_AVATAR_CONTENT',$oldContent,'Process')
}

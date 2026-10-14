# Builds the Avatar Editor recompilation (github.com/panedivitasei/AvatarEditorRecomp,
# commit c791019, GPL-3.0) from work\avatar-editor. Windows PowerShell 5.1.
# Run through build_avatar_editor.cmd (it sets up the Visual Studio environment).
#
#   work\avatar-editor\AvatarEditorRecomp-c791019.zip   editor source
#   work\avatar-editor\assets\                          your 2.0.12581.0 dashboard files
#   work\avatar-editor\AvatarEditorRecomp\              unpacked source + build (created)
# Result: work\avatar-editor\b\avatareditor.exe
# (A short build folder: the default out\build\win-amd64-release one puts some
# object files past Windows' 250-character path limit.)
param([switch]$Clean)
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$root = Split-Path (Split-Path $here -Parent) -Parent
# Set these to your own installs: REXGLUE_SDK = the ReXGlue SDK 0.10.0 win-amd64
# folder; LLVM_BIN = the bin folder of LLVM/clang 23.
$sdk = $env:REXGLUE_SDK
$llvm = $env:LLVM_BIN
if (-not $sdk -or -not $llvm) { Write-Output 'Set REXGLUE_SDK and LLVM_BIN first (see README).'; exit 1 }
$vsTools = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake'
# The LLVM 23 folder holds only the compilers and lld; the archiver (llvm-ar)
# comes from Visual Studio's own LLVM ("C++ Clang tools for Windows").
$vsLlvm = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin'
$src = Join-Path $here 'AvatarEditorRecomp'
$zip = Join-Path $here 'AvatarEditorRecomp-c791019.zip'
$bin = Join-Path $here 'b'
$stamp = Get-Date -Format yyyyMMdd-HHmmss
$diag = Join-Path $root 'outputs\diagnostics'
New-Item -ItemType Directory -Force $diag | Out-Null
$log = Join-Path $diag "build-avatar-editor-$stamp.log"

function Say([string]$text) { Write-Output $text; Add-Content -LiteralPath $log -Value $text }
function Run([string]$what, [scriptblock]$command) {
    Say "=== $what"
    & $command 2>&1 | ForEach-Object { $line = "$_"; Write-Output $line; Add-Content -LiteralPath $log -Value $line }
    if ($LASTEXITCODE -ne 0) { Say "FAILED: $what (exit $LASTEXITCODE). Log: $log"; exit 1 }
}

Say "Avatar Editor build $stamp"
foreach ($need in @(@('ReXGlue SDK', "$sdk\bin\rexglue.exe"), @('LLVM clang', "$llvm\clang++.exe"),
                    @('CMake', "$vsTools\CMake\bin\cmake.exe"), @('Ninja', "$vsTools\Ninja\ninja.exe"),
                    @('editor source zip', $zip), @('editor XEX', "$here\assets\avatareditor.xex"))) {
    if (-not (Test-Path -LiteralPath $need[1])) { Say "Missing $($need[0]): $($need[1])"; exit 1 }
}
# Library archiver: llvm-ar when present, otherwise Microsoft's lib.exe (always
# part of a Visual Studio C++ install; on PATH after vcvars64). CMake's clang
# rules call llvm-ar's "qc" form, so lib.exe gets its own rules (see below).
$ar = @("$llvm\llvm-ar.exe", "$vsLlvm\llvm-ar.exe") | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$useLib = $false
if (-not $ar) {
    if (-not (Get-Command lib.exe -ErrorAction SilentlyContinue)) {
        Say "No library archiver found (llvm-ar or lib.exe). Run this through build_avatar_editor.cmd, or install 'C++ Clang tools for Windows' in the Visual Studio Installer."
        exit 1
    }
    $useLib = $true
    Say "Archiver: lib.exe (Visual Studio)"
} else {
    Say "Archiver: $ar"
}
# clang, cmake and ninja first on PATH: the project's preset and the shader-pack script call them by name.
# Visual Studio's LLVM comes after LLVM 23 so the compilers stay version 23 and CMake still finds llvm-ar.
$env:PATH = "$llvm;$vsTools\CMake\bin;$vsTools\Ninja;$sdk\bin;$vsLlvm;$env:PATH"

if ($Clean -and (Test-Path $src)) { Say 'Removing the previous unpacked source and build'; Remove-Item $src -Recurse -Force }
if (-not (Test-Path (Join-Path $src 'ae_manifest.toml'))) {
    Say "Unpacking $zip"
    Expand-Archive -LiteralPath $zip -DestinationPath $here -Force
}
# The dashboard files go where the manifest and avatareditor.toml expect them (assets\).
Say 'Copying the dashboard files into the source tree'
Copy-Item (Join-Path $here 'assets\*') (Join-Path $src 'assets') -Recurse -Force

Push-Location $src
try {
    # Recompiles avatareditor.xex into generated\ae (C++). Takes a few minutes the first time.
    Run 'recompile the XEX (rexglue codegen)' { rexglue.exe codegen ae_manifest.toml }
    # Toolchain fixes, loaded after every project() call (CMAKE_PROJECT_INCLUDE):
    # 1. The installed SDK ships the imgui headers in its include folder but no
    #    'imgui' CMake target (that exists only when the SDK is built from
    #    source); the editor asks that target for the header folder.
    # 2. Static libraries: explicit archive commands with the archiver found
    #    above. CMake's own detection is not used: on the first configure it
    #    recorded "no archiver" in the build folder and keeps that value.
    $stub = Join-Path $here 'toolchain_fixes.cmake'
    $fix = @(
        '# Build fixes for this PC (written by build_avatar_editor.ps1).',
        'if(NOT TARGET imgui)',
        '    add_library(imgui INTERFACE)',
        '    target_include_directories(imgui INTERFACE "${AE_SDK_INCLUDE}")',
        'endif()')
    if ($useLib) {
        $create = 'lib.exe /nologo /out:<TARGET> <LINK_FLAGS> <OBJECTS>'
        $append = 'lib.exe /nologo /out:<TARGET> <TARGET> <LINK_FLAGS> <OBJECTS>'
        $finish = ''
    } else {
        $arPath = $ar -replace '\\', '/'
        $create = '\"' + $arPath + '\" qc <TARGET> <LINK_FLAGS> <OBJECTS>'
        $append = '\"' + $arPath + '\" q <TARGET> <LINK_FLAGS> <OBJECTS>'
        $finish = '\"' + $arPath + '\" s <TARGET>'
    }
    $fix += @(
        'foreach(lang C CXX ASM)',
        ('    set(CMAKE_${lang}_CREATE_STATIC_LIBRARY "' + $create + '")'),
        ('    set(CMAKE_${lang}_ARCHIVE_CREATE "' + $create + '")'),
        ('    set(CMAKE_${lang}_ARCHIVE_APPEND "' + $append + '")'),
        ('    set(CMAKE_${lang}_ARCHIVE_FINISH "' + $finish + '")'),
        'endforeach()')
    Set-Content -LiteralPath $stub -Encoding ASCII -Value $fix
    $arArgs = @()
    if (-not $useLib) { $arArgs = @("-DCMAKE_AR=$($ar -replace '\\', '/')") }
    # Config beside the exe; CMake only copies the default when none exists.
    # game_data_root must be absolute: the editor reads the asset packs from it
    # relative to the working folder, not the exe folder (2026-10-13 13:45).
    New-Item -ItemType Directory -Force $bin | Out-Null
    $toml = Join-Path $bin 'avatareditor.toml'
    $lines = if (Test-Path $toml) { Get-Content -LiteralPath $toml } else { Get-Content -LiteralPath (Join-Path $src 'avatareditor.toml') }
    $assetsDir = (Join-Path $src 'assets') -replace '\\', '/'
    $lines = $lines -replace '^game_data_root\s*=.*$', "game_data_root = `"$assetsDir`""
    Set-Content -LiteralPath $toml -Value $lines -Encoding ASCII
    $sdkInclude = ($sdk -replace '\\', '/') + '/include'
    $stubPath = $stub -replace '\\', '/'
    Run 'configure' { cmake --preset win-amd64-release -B $bin "-DCMAKE_PREFIX_PATH=$sdk" "-DCMAKE_PROJECT_INCLUDE=$stubPath" "-DAE_SDK_INCLUDE=$sdkInclude" @arArgs }
    Run 'build' { cmake --build $bin }
    # The runtime DLLs normally come with the build; copy them from the SDK if not.
    foreach ($dll in @('rexruntime.dll', 'TracyClient.dll')) {
        if (-not (Test-Path (Join-Path $bin $dll)) -and (Test-Path "$sdk\bin\$dll")) { Copy-Item "$sdk\bin\$dll" $bin }
    }
    # Shader pack: translates the editor's own shaders (from your XEX) for the
    # PC renderer. Same steps as tools\shaderpack\build_shaderpack.ps1, but the
    # XenosRecomp tool builds in a short folder with the same toolchain fixes.
    $image = Join-Path $here 'ae_image.bin'
    $xrBuild = Join-Path $here 'xr'
    $pack = Join-Path $bin 'shaderpack\ae.pack'
    Say '=== shader pack: image dump (avatareditor --dump_image_path)'
    Remove-Item $image -ErrorAction SilentlyContinue
    $dump = Start-Process -FilePath (Join-Path $bin 'avatareditor.exe') -ArgumentList "--dump_image_path=$image" -WorkingDirectory $bin -PassThru -WindowStyle Minimized
    if (-not $dump.WaitForExit(60000)) { Stop-Process -Id $dump.Id -Force; Say 'FAILED: image dump timed out'; exit 1 }
    if (-not (Test-Path $image)) { Say "FAILED: image dump (avatareditor.exe exit $($dump.ExitCode))"; exit 1 }
    Run 'shader pack: configure XenosRecomp' { cmake -S (Join-Path $src 'tools\XenosRecomp') -B $xrBuild -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_PROJECT_INCLUDE=$stubPath" "-DAE_SDK_INCLUDE=$sdkInclude" @arArgs }
    Run 'shader pack: build XenosRecomp' { cmake --build $xrBuild }
    New-Item -ItemType Directory -Force (Split-Path $pack) | Out-Null
    $xr = Join-Path $xrBuild 'XenosRecomp\XenosRecomp.exe'
    Run 'shader pack: translate' { & $xr --cc2-pack $image (Join-Path $src 'tools\shaderpack\ae_shader_deltas.csv') $pack }
    Remove-Item $image -ErrorAction SilentlyContinue
    Say "Shader pack: $pack"
} finally { Pop-Location }
Say "Avatar Editor built: $bin\avatareditor.exe"
Say "Log: $log"
exit 0

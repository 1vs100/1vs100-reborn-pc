# Import-AvatarCloset.ps1 -- copies Xbox 360 Avatar items you own into the
# Avatar Editor's closet, where the editor and 1 vs 100 both read them.
# Windows PowerShell 5.1.
#
#   Import-AvatarCloset.ps1                          asks for a folder
#   Import-AvatarCloset.ps1 -Source <folder> [<folder> ...] [-Closet <folder>]
#
# Point it at any folder holding Avatar item packages (LIVE/PIRS/CON files of
# content type 0x9000), however they are named or nested: a copy of your
# console's Content folder, a USB drive's Content folder copied to the PC, or
# items you saved with a tool such as Velocity or Horizon. Other files in the
# folder are ignored. The packages are only read.
#
# Writes, in the closet folder: <asset-guid>.bin (the item's asset_v2.bin),
# icons\<asset-guid>.png and closet_index.tsv (guid, categories, bodies, name),
# the format the Avatar Editor's closet.cpp reads, plus last-import.txt (one
# summary line for the launcher). Items already in the closet are replaced by a
# newly imported copy; other closet items are kept.
param(
    [string[]]$Source = @(),
    [string]$Closet = '',
    [string]$EditorConfig = ''
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
if (-not $Closet) { $Closet = Join-Path $here 'closet' }
if (-not $EditorConfig) { $EditorConfig = Join-Path $here 'b\avatareditor.toml' }
$root = Split-Path (Split-Path $here -Parent) -Parent
$diag = Join-Path $root 'outputs\diagnostics'
if (-not (Test-Path -LiteralPath $diag)) { $diag = $Closet }
New-Item -ItemType Directory -Force (Join-Path $Closet 'icons') | Out-Null
$log = Join-Path $diag ("import-avatar-closet-" + (Get-Date -Format yyyyMMdd-HHmmss) + ".log")
function Say([string]$text) { Write-Output $text; Add-Content -LiteralPath $log -Value $text -Encoding UTF8 }

function U32BE([byte[]]$b, [int]$o) { return ([long]$b[$o] -shl 24) -bor ([long]$b[$o + 1] -shl 16) -bor ([long]$b[$o + 2] -shl 8) -bor [long]$b[$o + 3] }
function U24LE([byte[]]$b, [int]$o) { return [long]$b[$o] -bor ([long]$b[$o + 1] -shl 8) -bor ([long]$b[$o + 2] -shl 16) }
function Div([long]$a, [long]$b) { return [long][Math]::Floor($a / $b) }

# STFS (the Xbox 360 package format): blocks of 0x1000 bytes with hash tables
# interleaved; a file's blocks are chained through the hash entries.
function Open-Stfs([byte[]]$d) {
    $headerSize = U32BE $d 0x340
    $first = ($headerSize + 0xFFF) -band 0xF000
    $shift = if ($first -eq 0xB000) { 0 } else { 1 }
    $step0 = if ($shift -eq 0) { 0xAB } else { 0xAC }
    return @{ D = $d; First = $first; Shift = $shift; Step0 = $step0
              FtCount = [int]($d[0x37C] -bor ([int]$d[0x37D] -shl 8)); FtBlock = (U24LE $d 0x37E) }
}
function BlockOffset($s, [long]$b) {
    $sh = $s.Shift
    $r = ((Div ($b + 0xAA) 0xAA) -shl $sh) + $b
    if ($b -ge 0xAA) {
        if ($b -lt 0x70E4) { $r += (Div ($b + 0x70E4) 0x70E4) -shl $sh }
        else { $r = (1 -shl $sh) + $r + ((Div ($b + 0x70E4) 0x70E4) -shl $sh) }
    }
    return ($r -shl 12) + $s.First
}
function NextBlock($s, [long]$b) {
    $n = (Div $b 0xAA) * $s.Step0
    if ($b -ge 0xAA) {
        $n += ((Div $b 0x70E4) + 1) -shl $s.Shift
        if ($b -ge 0x70E4) { $n += 1 -shl $s.Shift }
    }
    $o = ($n -shl 12) + $s.First + ($b % 0xAA) * 0x18
    return ([long]$s.D[$o + 0x15] -shl 16) -bor ([long]$s.D[$o + 0x16] -shl 8) -bor [long]$s.D[$o + 0x17]
}
function ReadBlocks($s, [long]$start, [long]$count, [bool]$consecutive, [long]$size) {
    $out = New-Object byte[] ($count * 0x1000)
    $b = $start
    for ($i = 0; $i -lt $count; $i++) {
        $o = BlockOffset $s $b
        if ($o + 0x1000 -gt $s.D.Length) { throw "block $b is past the end of the package (damaged or cut short)" }
        [Array]::Copy($s.D, $o, $out, $i * 0x1000, 0x1000)
        $b = if ($consecutive) { $b + 1 } else { NextBlock $s $b }
    }
    if ($size -ge 0 -and $size -lt $out.Length) { $trim = New-Object byte[] $size; [Array]::Copy($out, $trim, $size); return ,$trim }
    return ,$out
}
function Stfs-Files($s) {
    $table = ReadBlocks $s $s.FtBlock $s.FtCount $false -1
    $files = @{}
    for ($i = 0; $i + 0x40 -le $table.Length; $i += 0x40) {
        $flags = $table[$i + 0x28]; $n = $flags -band 0x3F
        if ($n -eq 0) { continue }
        if ($flags -band 0x80) { continue }   # folder
        $name = [System.Text.Encoding]::ASCII.GetString($table, $i, $n)
        $files[$name.ToLowerInvariant()] = @{ Blocks = (U24LE $table ($i + 0x29)); Start = (U24LE $table ($i + 0x2F))
                                              Consecutive = [bool]($flags -band 0x40); Size = (U32BE $table ($i + 0x34)) }
    }
    return $files
}
# First bytes of a file, to skip everything that is not an Avatar item package
# without reading it whole (a console Content folder holds large games too).
function Read-Head([string]$path, [int]$count) {
    $stream = [System.IO.File]::OpenRead($path)
    try {
        $head = New-Object byte[] $count
        $read = $stream.Read($head, 0, $count)
        if ($read -lt $count) { return $null }
        return ,$head
    } finally { $stream.Dispose() }
}
function Guid-Text([byte[]]$id) {
    $hex = -join ($id | ForEach-Object { $_.ToString('x2') })
    return '{0}-{1}-{2}-{3}-{4}' -f $hex.Substring(0, 8), $hex.Substring(8, 4), $hex.Substring(12, 4), $hex.Substring(16, 4), $hex.Substring(20, 12)
}
# The package's display name (UTF-16BE at 0x411); folder or file names are a
# fallback, and console folder names (title IDs, 00009000) are not used.
function Item-Name([byte[]]$d, $file) {
    $name = [System.Text.Encoding]::BigEndianUnicode.GetString($d, 0x411, 0x80)
    $cut = $name.IndexOf([char]0)
    if ($cut -ge 0) { $name = $name.Substring(0, $cut) }
    $name = $name.Trim().TrimStart('/').Trim()
    # Some repackaged items carry web-encoded names ("S%C3%ADles Hennin").
    if ($name -match '%[0-9A-Fa-f]{2}') { try { $name = [System.Uri]::UnescapeDataString($name) } catch { } }
    if (-not $name) {
        $folder = $file.Directory.Name
        if ($folder -match '%[0-9A-Fa-f]{2}') { try { $folder = [System.Uri]::UnescapeDataString($folder) } catch { } }
        if ($folder -notmatch '^[0-9A-Fa-f]{8}$' -and $folder -notmatch '^[0-9A-Fa-f]{16}$') { $name = $folder.Trim() }
    }
    return ($name -replace "[`t`r`n]", ' ')
}

if ($Source.Count -eq 0) {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object System.Windows.Forms.FolderBrowserDialog
    $picker.Description = 'Choose a folder with your Xbox 360 Avatar items (for example a copy of your console''s Content folder).'
    if ($picker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { Write-Output 'No folder chosen.'; exit 2 }
    $Source = @($picker.SelectedPath)
}

Say "Avatar closet import $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
foreach ($folder in $Source) { Say "Source: $folder" }
Say "Closet: $Closet"
$missing = @($Source | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missing.Count) { Say "Folder not found: $($missing -join ', ')"; Set-Content -LiteralPath (Join-Path $Closet 'last-import.txt') -Value "Folder not found: $($missing[0])"; exit 1 }

# Existing index rows (earlier imports) are kept unless re-imported.
$index = [ordered]@{}
$indexPath = Join-Path $Closet 'closet_index.tsv'
if (Test-Path -LiteralPath $indexPath) {
    foreach ($line in [System.IO.File]::ReadAllLines($indexPath, (New-Object System.Text.UTF8Encoding($false)))) {
        $guid = ($line -split "`t")[0]
        if ($guid.Length -eq 36) { $index[$guid] = $line }
    }
}
$before = $index.Count
$imported = 0; $icons = 0; $skipped = 0; $dupes = 0; $scanned = 0
$seen = @{}
$candidates = @(foreach ($folder in $Source) {
    Get-ChildItem -LiteralPath $folder -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Length -ge 0x2000 -and $_.Length -le 64MB }
})
Say "Files to check: $($candidates.Count)"
foreach ($file in $candidates) {
    $head = Read-Head $file.FullName 0x3F1
    if (-not $head) { continue }
    $magic = [System.Text.Encoding]::ASCII.GetString($head, 0, 4)
    if ($magic -ne 'LIVE' -and $magic -ne 'PIRS' -and $magic -ne 'CON ') { continue }
    if ((U32BE $head 0x344) -ne 0x9000) { continue }
    $scanned++
    try {
        # The item's asset ID is in the package header (Avatar metadata, 0x3E1).
        # Marketplace downloads are also named by it: used when the header has none.
        $id = New-Object byte[] 16
        [Array]::Copy($head, 0x3E1, $id, 0, 16)
        if (-not ($id | Where-Object { $_ })) {
            if ($file.Name -notmatch '^[0-9A-Fa-f]{32}$') { throw 'no asset ID in the package' }
            for ($i = 0; $i -lt 16; $i++) { $id[$i] = [Convert]::ToByte($file.Name.Substring($i * 2, 2), 16) }
        }
        $guid = Guid-Text $id
        if ($seen.ContainsKey($guid)) { $dupes++; continue }
        $d = [System.IO.File]::ReadAllBytes($file.FullName)
        $s = Open-Stfs $d
        $files = Stfs-Files $s
        $asset = $files['asset_v2.bin']
        if (-not $asset) { throw 'no asset_v2.bin inside' }
        $blob = ReadBlocks $s $asset.Start $asset.Blocks $asset.Consecutive $asset.Size
        if ($blob.Length -lt 0x40 -or [System.Text.Encoding]::ASCII.GetString($blob, 0, 4) -ne 'STRB') { throw 'asset_v2.bin is not an Avatar asset (STRB)' }
        [System.IO.File]::WriteAllBytes((Join-Path $Closet "$guid.bin"), $blob)
        $icon = $files['icon.png']
        if ($icon -and $icon.Size -gt 8) {
            $png = ReadBlocks $s $icon.Start $icon.Blocks $icon.Consecutive $icon.Size
            if ($png[0] -eq 0x89 -and $png[1] -eq 0x50 -and $png[2] -eq 0x4E -and $png[3] -eq 0x47) {
                [System.IO.File]::WriteAllBytes((Join-Path $Closet "icons\$guid.png"), $png); $icons++
            }
        }
        # The asset ID carries both masks: categories = first 4 bytes, bodies = bytes 6-7 & 0xF.
        $categories = U32BE $id 0
        $bodies = $id[7] -band 0xF
        $name = Item-Name $d $file
        if (-not $name) { $name = $guid }
        $index[$guid] = "$guid`t$('{0:x8}' -f $categories)`t$bodies`t$name"
        $seen[$guid] = $true
        $imported++
    } catch {
        $skipped++
        Say "skipped $($file.FullName): $($_.Exception.Message)"
    }
}
$lines = @($index.Keys | Sort-Object | ForEach-Object { $index[$_] })
[System.IO.File]::WriteAllText($indexPath, (($lines -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding($false)))
Say "Avatar item packages found: $scanned"
Say "Imported: $imported items ($icons icons); skipped: $skipped; duplicates: $dupes"
Say "Closet: $($lines.Count) items (was $before)"
$summary = if ($scanned -eq 0) { "No Avatar items found in that folder. Closet: $($lines.Count) items." }
           else { "Imported $imported Avatar items ($skipped skipped). Closet: $($lines.Count) items." }
Set-Content -LiteralPath (Join-Path $Closet 'last-import.txt') -Value $summary -Encoding UTF8

# Point the editor at this closet (its config sits beside avatareditor.exe).
if (Test-Path -LiteralPath $EditorConfig) {
    $closetPath = (Resolve-Path -LiteralPath $Closet).Path -replace '\\', '/'
    $text = @(Get-Content -LiteralPath $EditorConfig)
    if ($text -match '^avatar_closet_dir\s*=') {
        $text = $text -replace '^avatar_closet_dir\s*=.*$', "avatar_closet_dir = `"$closetPath`""
    } else {
        $text += "avatar_closet_dir = `"$closetPath`""
    }
    Set-Content -LiteralPath $EditorConfig -Value $text -Encoding ASCII
    Say "avatareditor.toml: avatar_closet_dir = $closetPath"
}
Say "Log: $log"
exit 0

# Export-GameContent.ps1 -- writes every file inside the game's UI packages
# (launch.xzp, episode.xzp) to mods\_export so logos, images and text can be
# looked at, with an index of every image and its size. Read-only for the game:
# it reads the original packages in work\native-parity and changes nothing.
# Launcher: Debug tab > "Export game images & text". Windows PowerShell 5.1.
#
# mods\_export\launch\...      files of launch.xzp (string tables also as .xus.txt)
# mods\_export\episode\...     files of episode.xzp
# mods\_export\INDEX.html      gallery of every image (open in a browser)
# mods\_export\INDEX.txt       the same as text: package, path, kind, size
param([string]$OutDir='')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(-not $OutDir) { $OutDir=Join-Path $root 'mods\_export' }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$logPath=Join-Path $OutDir 'export-log.txt'
function Log([string]$text) { Write-Output $text; Add-Content -LiteralPath $logPath -Value $text -Encoding UTF8 }
Set-Content -LiteralPath $logPath -Value ("Export " + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')) -Encoding UTF8
. (Join-Path $PSScriptRoot 'XzpMods.ps1')

# Image kind and size from the first bytes of a package file.
function ImageInfo([byte[]]$b) {
    if($b.Length -ge 24 -and $b[0] -eq 0x89 -and $b[1] -eq 0x50 -and $b[2] -eq 0x4E -and $b[3] -eq 0x47) {
        return @('PNG', (XzpU32 $b 16), (XzpU32 $b 20))
    }
    if($b.Length -ge 16 -and [System.Text.Encoding]::ASCII.GetString($b, 0, 4) -eq '.mct') {
        # Xbox 360 compressed texture: the size is packed in the dword at offset 12.
        $v=XzpU32 $b 12
        return @('XBOX', (($v -band 0x1FFF) + 1), ((($v -shr 13) -band 0x1FFF) + 1))
    }
    if($b.Length -ge 4 -and $b[0] -eq 0xFF -and $b[1] -eq 0xD8) {
        $p=2
        while($p + 9 -lt $b.Length) {
            if($b[$p] -ne 0xFF) { $p++; continue }
            $marker=$b[$p + 1]
            if($marker -in 0xC0,0xC1,0xC2) { return @('JPG', (XzpU16 $b ($p + 7)), (XzpU16 $b ($p + 5))) }
            if($marker -eq 0xD8 -or ($marker -ge 0xD0 -and $marker -le 0xD7) -or $marker -eq 0x01) { $p+=2; continue }
            $p+=2 + (XzpU16 $b ($p + 2))
        }
        return @('JPG', 0, 0)
    }
    return $null
}

try {
    $reference=Join-Path $root 'work\native-parity\assets'
    $packages=@(@('launch', (Join-Path $reference 'media\launch.xzp')), @('episode', (Join-Path $reference 'media\episode.xzp')))
    $rows=New-Object System.Collections.Generic.List[object]
    foreach($pkg in $packages) {
        $name=$pkg[0]; $path=$pkg[1]
        if(-not (Test-Path -LiteralPath $path)) { Log "missing: $path"; continue }
        $target=Join-Path $OutDir $name
        if(Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
        $count=Export-XzpContents $path $target
        Log "$name.xzp: $count files -> $target"
        foreach($f in (Read-Xzp $path).Files) {
            $info=ImageInfo $f.Data
            if($info) { $rows.Add([pscustomobject]@{ Package=$name; Path=$f.Name; Kind=$info[0]; Width=$info[1]; Height=$info[2]; Bytes=$f.Data.Length }) }
        }
    }

    $text=New-Object System.Collections.Generic.List[string]
    $text.Add("Images inside the game's UI packages. To replace one, save a PNG of the same size at")
    $text.Add("  mods\<package>\files\<path>      e.g. mods\launch\files\assets\intro_logo.png")
    $text.Add("PNG and JPG entries can be opened directly from this folder. XBOX entries are")
    $text.Add("Xbox 360 compressed textures (.mct, no preview); a PNG of the listed size under the")
    $text.Add("same name is expected to work but is experimental: check the log line 'Mods:' and")
    $text.Add("the screen. 'Play without mods' on the Debug tab goes back to the originals.")
    $text.Add('')
    $text.Add("package`tpath`tkind`twidth x height`tbytes")
    foreach($r in $rows) { $text.Add("$($r.Package)`t$($r.Path)`t$($r.Kind)`t$($r.Width) x $($r.Height)`t$($r.Bytes)") }
    [System.IO.File]::WriteAllLines((Join-Path $OutDir 'INDEX.txt'), [string[]]$text, (New-Object System.Text.UTF8Encoding($true)))

    $html=New-Object System.Text.StringBuilder
    [void]$html.Append('<!doctype html><meta charset="utf-8"><title>1 vs 100 images</title><style>body{font:14px Segoe UI,sans-serif;background:#1b1b22;color:#eee;margin:16px}div.i{display:inline-block;vertical-align:top;width:220px;margin:6px;padding:8px;background:#2a2a35;border-radius:6px;word-break:break-all}img{max-width:200px;max-height:150px;background:repeating-conic-gradient(#555 0 25%,#777 0 50%) 0 0/16px 16px}code{color:#9cf}.x{height:80px;display:flex;align-items:center;justify-content:center;background:#333;color:#aaa}</style>')
    [void]$html.Append('<h1>1 vs 100 images</h1><p>To replace one, save a PNG of the same size at <code>mods\&lt;package&gt;\files\&lt;path&gt;</code>. Grey boxes are Xbox textures (no preview; replacing them is experimental).</p>')
    foreach($r in $rows) {
        $rel=($r.Package + '/' + ($r.Path -replace '\\','/'))
        $enc=(($rel -split '/') | ForEach-Object { [System.Uri]::EscapeDataString($_) }) -join '/'
        $label=[System.Net.WebUtility]::HtmlEncode("$($r.Package)\$($r.Path)")
        $preview=if($r.Kind -eq 'XBOX'){ '<div class="x">Xbox texture</div>' } else { "<img src=""$enc"" loading=""lazy"">" }
        [void]$html.Append("<div class=""i"">$preview<br><code>$label</code><br>$($r.Kind) $($r.Width) x $($r.Height)</div>")
    }
    [System.IO.File]::WriteAllText((Join-Path $OutDir 'INDEX.html'), $html.ToString(), (New-Object System.Text.UTF8Encoding($false)))

    # The mods folder's README (kept if the player has edited it).
    $readme=Join-Path (Split-Path $OutDir -Parent) 'README.txt'
    $source=Join-Path $PSScriptRoot 'mods-README.txt'
    if(-not (Test-Path -LiteralPath $readme) -and (Test-Path -LiteralPath $source)) { Copy-Item -LiteralPath $source $readme }
    Log "images: $($rows.Count) (index: INDEX.html, INDEX.txt)"
    exit 0
} catch {
    Log "export failed: $($_.Exception.Message)"
    exit 1
}

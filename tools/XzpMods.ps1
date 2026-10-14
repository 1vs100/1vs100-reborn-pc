# XzpMods.ps1 -- reads and writes the game's XUI packages (.xzp) and their string
# tables (.xus), applies mods, and exports package contents for inspection.
# Windows PowerShell 5.1 compatible. Dot-source it:  . .\XzpMods.ps1
#
# Formats (verified 2026-10-11: both packages and every string table rebuild
# byte for byte):
#   .xzp  'XUIZ', u32 version, u32 total size, u32 0, u32 table size, u16 count,
#         then per file: u32 size, u32 offset (from the end of the table),
#         u8 name length, UTF-16BE name; then the file data in table order.
#   .xus  'XUIS', u16 version, u32 total size, u16 count, then per string:
#         u16 length + UTF-16BE value, u16 length + UTF-16BE key.
#
# Mods folder layout (any number of mod roots, applied in order):
#   <root>\launch\files\<path inside launch.xzp>    replaces that file
#   <root>\launch\text\*.json                       replaces strings
#   <root>\episode\files\... , <root>\episode\text\*.json  (episode.xzp)
# Text JSON: { "tables": { "<lang or *>\\<Table>.xus": { "<key>": "<value>" } } }
# A key ending in * matches every key that starts with the text before it.

function XzpU16([byte[]]$b, [int]$o) { return ([int]$b[$o] * 256) + [int]$b[$o + 1] }
function XzpU32([byte[]]$b, [int]$o) {
    return ([long]$b[$o] * 16777216) + ([long]$b[$o + 1] * 65536) + ([long]$b[$o + 2] * 256) + [long]$b[$o + 3]
}
function XzpBE16([int]$v) { return [byte[]]@((($v -shr 8) -band 255), ($v -band 255)) }
function XzpBE32([long]$v) {
    return [byte[]]@((($v -shr 24) -band 255), (($v -shr 16) -band 255), (($v -shr 8) -band 255), ($v -band 255))
}
$script:XzpUtf16 = [System.Text.Encoding]::BigEndianUnicode

function Read-Xzp([string]$Path) {
    $b = [System.IO.File]::ReadAllBytes($Path)
    if ($b.Length -lt 22 -or [System.Text.Encoding]::ASCII.GetString($b, 0, 4) -ne 'XUIZ') { throw "Not an XUI package: $Path" }
    $count = XzpU16 $b 20
    $p = 22
    $entries = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $count; $i++) {
        $size = XzpU32 $b $p; $offset = XzpU32 $b ($p + 4); $n = [int]$b[$p + 8]
        $name = $script:XzpUtf16.GetString($b, $p + 9, $n * 2)
        $p += 9 + $n * 2
        $entries.Add(@($name, $size, $offset))
    }
    $base = $p
    $files = New-Object System.Collections.Generic.List[object]
    foreach ($e in $entries) {
        $data = New-Object byte[] ([int]$e[1])
        [System.Array]::Copy($b, [long]($base + $e[2]), $data, 0, [int]$e[1])
        $files.Add([pscustomobject]@{ Name = $e[0]; Data = $data })
    }
    $head = New-Object byte[] 16
    [System.Array]::Copy($b, 0, $head, 0, 16)
    return [pscustomobject]@{ Head = $head; Files = $files }
}

function Write-Xzp($Package, [string]$Path) {
    $table = New-Object System.IO.MemoryStream
    $offset = [long]0
    foreach ($f in $Package.Files) {
        $nameBytes = $script:XzpUtf16.GetBytes($f.Name)
        if ($f.Name.Length -gt 255) { throw "Package file name too long: $($f.Name)" }
        $table.Write((XzpBE32 $f.Data.Length), 0, 4)
        $table.Write((XzpBE32 $offset), 0, 4)
        $table.WriteByte([byte]$f.Name.Length)
        $table.Write($nameBytes, 0, $nameBytes.Length)
        $offset += $f.Data.Length
    }
    $tableBytes = $table.ToArray()
    $total = 22 + $tableBytes.Length + $offset
    $out = New-Object System.IO.MemoryStream
    $out.Write($Package.Head, 0, 8)                      # magic + version
    $out.Write((XzpBE32 $total), 0, 4)
    $out.Write($Package.Head, 12, 4)                     # reserved
    $out.Write((XzpBE32 $tableBytes.Length), 0, 4)
    $out.Write((XzpBE16 $Package.Files.Count), 0, 2)
    $out.Write($tableBytes, 0, $tableBytes.Length)
    foreach ($f in $Package.Files) { $out.Write($f.Data, 0, $f.Data.Length) }
    [System.IO.File]::WriteAllBytes($Path, $out.ToArray())
}

function Read-Xus([byte[]]$b) {
    if ($b.Length -lt 12 -or [System.Text.Encoding]::ASCII.GetString($b, 0, 4) -ne 'XUIS') { throw 'Not an XUI string table' }
    $version = XzpU16 $b 4
    $count = XzpU16 $b 10
    $p = 12
    $strings = New-Object System.Collections.Generic.List[object]
    for ($i = 0; $i -lt $count; $i++) {
        $n = XzpU16 $b $p; $value = $script:XzpUtf16.GetString($b, $p + 2, $n * 2); $p += 2 + $n * 2
        $n = XzpU16 $b $p; $key = $script:XzpUtf16.GetString($b, $p + 2, $n * 2); $p += 2 + $n * 2
        $strings.Add([pscustomobject]@{ Key = $key; Value = $value })
    }
    return [pscustomobject]@{ Version = $version; Strings = $strings }
}

function Write-Xus($Table) {
    $body = New-Object System.IO.MemoryStream
    foreach ($s in $Table.Strings) {
        foreach ($text in @($s.Value, $s.Key)) {
            if ($text.Length -gt 65535) { throw "String too long: $($s.Key)" }
            $bytes = $script:XzpUtf16.GetBytes($text)
            $body.Write((XzpBE16 $text.Length), 0, 2)
            $body.Write($bytes, 0, $bytes.Length)
        }
    }
    $bodyBytes = $body.ToArray()
    $out = New-Object System.IO.MemoryStream
    $out.Write([System.Text.Encoding]::ASCII.GetBytes('XUIS'), 0, 4)
    $out.Write((XzpBE16 $Table.Version), 0, 2)
    $out.Write((XzpBE32 (12 + $bodyBytes.Length)), 0, 4)
    $out.Write((XzpBE16 $Table.Strings.Count), 0, 2)
    $out.Write($bodyBytes, 0, $bodyBytes.Length)
    return $out.ToArray()
}

# Applies every mod root's files and text for one package. Returns a summary.
function Invoke-XzpMods([string]$PackagePath, [string]$PackageName, [string[]]$ModRoots) {
    $package = Read-Xzp $PackagePath
    $byName = @{}
    foreach ($f in $package.Files) { $byName[$f.Name.ToLowerInvariant()] = $f }
    $filesReplaced = 0; $stringsChanged = 0; $notes = New-Object System.Collections.Generic.List[string]
    foreach ($root in $ModRoots) {
        if (-not $root -or -not (Test-Path -LiteralPath $root)) { continue }
        $filesDir = Join-Path $root "$PackageName\files"
        if (Test-Path -LiteralPath $filesDir) {
            $prefix = (Resolve-Path -LiteralPath $filesDir).ProviderPath.TrimEnd('\', '/')
            foreach ($file in Get-ChildItem -LiteralPath $filesDir -Recurse -File) {
                # Package names use backslashes.
                $relative = $file.FullName.Substring($prefix.Length + 1) -replace '/', '\'
                $target = $byName[$relative.ToLowerInvariant()]
                if ($target) {
                    $target.Data = [System.IO.File]::ReadAllBytes($file.FullName); $filesReplaced++
                } else {
                    $notes.Add("not in $PackageName package, skipped: $relative")
                }
            }
        }
        $textDir = Join-Path $root "$PackageName\text"
        if (Test-Path -LiteralPath $textDir) {
            foreach ($json in Get-ChildItem -LiteralPath $textDir -Filter *.json -File) {
                $doc = Get-Content -LiteralPath $json.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
                if (-not $doc.tables) { $notes.Add("no 'tables' in $($json.Name)"); continue }
                foreach ($tableSpec in $doc.tables.PSObject.Properties) {
                    $spec = $tableSpec.Name
                    $slash = $spec.IndexOf('\')
                    if ($slash -lt 1) { $notes.Add("bad table name '$spec' in $($json.Name)"); continue }
                    $lang = $spec.Substring(0, $slash).ToLowerInvariant()
                    $tableName = $spec.Substring($slash + 1).ToLowerInvariant()
                    $overrides = @($tableSpec.Value.PSObject.Properties)
                    foreach ($f in $package.Files) {
                        $name = $f.Name.ToLowerInvariant()
                        $match = if ($lang -eq '*') { $name.EndsWith('\' + $tableName) } else { $name -eq ($lang + '\' + $tableName) }
                        if (-not $match) { continue }
                        $table = Read-Xus $f.Data
                        $changed = 0
                        foreach ($s in $table.Strings) {
                            foreach ($o in $overrides) {
                                $key = $o.Name
                                $hit = if ($key.EndsWith('*')) { $s.Key.StartsWith($key.Substring(0, $key.Length - 1)) } else { $s.Key -eq $key }
                                if ($hit -and $s.Value -ne [string]$o.Value) { $s.Value = [string]$o.Value; $changed++ }
                            }
                        }
                        if ($changed) { $f.Data = Write-Xus $table; $stringsChanged += $changed }
                    }
                }
            }
        }
    }
    if ($filesReplaced -or $stringsChanged) { Write-Xzp $package $PackagePath }
    return [pscustomobject]@{ Package = $PackageName; FilesReplaced = $filesReplaced; StringsChanged = $stringsChanged; Notes = $notes }
}

# Writes every file in a package to a folder; string tables also as readable text.
function Export-XzpContents([string]$PackagePath, [string]$OutDir) {
    $package = Read-Xzp $PackagePath
    $count = 0
    foreach ($f in $package.Files) {
        $target = Join-Path $OutDir $f.Name
        New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
        [System.IO.File]::WriteAllBytes($target, $f.Data)
        if ($f.Name.ToLowerInvariant().EndsWith('.xus')) {
            $lines = foreach ($s in (Read-Xus $f.Data).Strings) {
                $s.Key + "`t" + ($s.Value -replace "`r", '\r' -replace "`n", '\n' -replace "`t", '\t')
            }
            [System.IO.File]::WriteAllLines($target + '.txt', [string[]]$lines, (New-Object System.Text.UTF8Encoding($true)))
        }
        $count++
    }
    return $count
}

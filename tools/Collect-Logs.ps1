# Collect-Logs.ps1 -- zips the newest game logs onto the Desktop for a bug report
# (launcher > Play > "Send logs"). Windows PowerShell 5.1.
#
# Includes: the two newest game logs (native-room-*.log, with their rolled-over
# parts) and the newest crash dumps/stacks from outputs\diagnostics. Leaves
# out player-data (profile, Avatar, saved stats). Game logs name your gamertag;
# nothing else personal.
# Writes the zip's path to outputs\diagnostics\last-logs.txt for the launcher.
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$diag = Join-Path $root 'outputs\diagnostics'
$result = Join-Path $diag 'last-logs.txt'
try {
    if (-not (Test-Path -LiteralPath $diag)) { throw 'No outputs\diagnostics folder yet: start the game once first.' }
    Remove-Item -LiteralPath $result -ErrorAction SilentlyContinue
    $all = @(Get-ChildItem -LiteralPath $diag -File)
    # A game log and its rolled-over parts share a base name (name.log, name.1.log, ...).
    $base = { param($f) $f.Name -replace '(\.\d+)?\.log$', '' }
    $runs = @($all | Where-Object { $_.Name -match '^native-room-.*\.log$' } |
              Group-Object -Property { & $base $_ } |
              Sort-Object { ($_.Group | Measure-Object LastWriteTime -Maximum).Maximum } -Descending |
              Select-Object -First 2)
    $files = @($runs | ForEach-Object { $_.Group })
    $files += @($all | Where-Object { $_.Extension -in '.dmp', '.txt' -and $_.Name -match 'crash|stack' } |
                Sort-Object LastWriteTime -Descending | Select-Object -First 3)
    if ($files.Count -eq 0) { throw 'No game logs yet: play once, then try again.' }
    $desktop = [Environment]::GetFolderPath('Desktop')
    if (-not $desktop -or -not (Test-Path -LiteralPath $desktop)) { $desktop = $root }
    $zip = Join-Path $desktop ('1vs100-logs-' + (Get-Date -Format yyyyMMdd-HHmmss) + '.zip')
    # Copy first: the game may still be writing its current log.
    $temp = Join-Path ([IO.Path]::GetTempPath()) ('1vs100-logs-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $temp | Out-Null
    try {
        foreach ($file in $files) {
            $stream = [IO.File]::Open($file.FullName, 'Open', 'Read', 'ReadWrite')
            try {
                $out = [IO.File]::Create((Join-Path $temp $file.Name))
                try { $stream.CopyTo($out) } finally { $out.Dispose() }
            } finally { $stream.Dispose() }
        }
        Compress-Archive -Path (Join-Path $temp '*') -DestinationPath $zip -CompressionLevel Optimal -Force
    } finally { Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue }
    $size = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)
    Set-Content -LiteralPath $result -Value ("ok|$zip|$($files.Count) files, $size MB") -Encoding UTF8
    Write-Output "Logs: $zip ($($files.Count) files, $size MB)"
    exit 0
} catch {
    if (Test-Path -LiteralPath $diag) { Set-Content -LiteralPath $result -Value ("error|" + $_.Exception.Message) -Encoding UTF8 }
    Write-Output $_.Exception.Message
    exit 1
}

<#
.SYNOPSIS
    Merges shared squad note files into the addon's squad_notes.json.

.DESCRIPTION
    Collects every squad_notes_*.json in the target directory (files shared by other
    players) together with the existing squad_notes.json and writes the combined
    result back to squad_notes.json.

    Merge rules per account:
      note           distinct non-empty notes joined with " | "
      last_seen      the most recent timestamp wins
      last_character the character belonging to that most recent timestamp
      times_seen     the highest count (not summed, so re-merging stays stable)

.EXAMPLE
    .\merge_squad_notes.ps1
    Merges everything in the current directory.

.EXAMPLE
    .\merge_squad_notes.ps1 -Path "$env:APPDATA\..\..\Guild Wars 2\addons\GW2HB"
#>
[CmdletBinding()]
param(
    [string]$Path = (Get-Location).Path,
    [switch]$NoBackup
)

$ErrorActionPreference = 'Stop'

# notes coming from different files are concatenated with this marker
$NoteSeparator = ' | '

if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
    throw "Directory not found: $Path"
}

$root = (Resolve-Path -LiteralPath $Path).Path
$target = Join-Path $root 'squad_notes.json'

$sources = @()
if (Test-Path -LiteralPath $target -PathType Leaf) { $sources += $target }
$sources += Get-ChildItem -LiteralPath $root -Filter 'squad_notes_*.json' -File |
            Sort-Object Name |
            Select-Object -ExpandProperty FullName

if ($sources.Count -eq 0) {
    Write-Warning "No squad_notes.json or squad_notes_*.json found in $root"
    return
}

function Get-Field {
    param($Entry, [string]$Name, $Default)
    $prop = $Entry.PSObject.Properties[$Name]
    if ($null -eq $prop -or $null -eq $prop.Value) { return $Default }
    return $prop.Value
}

$merged = @{}
$fileCount = 0

foreach ($file in $sources) {
    try {
        $raw = Get-Content -LiteralPath $file -Raw -Encoding UTF8
    }
    catch {
        Write-Warning "Could not read $(Split-Path $file -Leaf): $($_.Exception.Message)"
        continue
    }

    if ([string]::IsNullOrWhiteSpace($raw)) { continue }

    try {
        $data = $raw.TrimStart([char]0xFEFF) | ConvertFrom-Json
    }
    catch {
        Write-Warning "Skipping $(Split-Path $file -Leaf): not valid JSON ($($_.Exception.Message))"
        continue
    }

    if ($null -eq $data -or $data -isnot [System.Management.Automation.PSCustomObject]) {
        Write-Warning "Skipping $(Split-Path $file -Leaf): expected a JSON object of accounts"
        continue
    }

    $fileCount++
    $entryCount = 0

    foreach ($account in $data.PSObject.Properties) {
        $name = $account.Name
        $entry = $account.Value
        if ([string]::IsNullOrWhiteSpace($name) -or $null -eq $entry -or $entry -isnot [System.Management.Automation.PSCustomObject]) { continue }
        $entryCount++

        $note = [string](Get-Field $entry 'note' '')
        $lastCharacter = [string](Get-Field $entry 'last_character' '')
        $lastSeen = [string](Get-Field $entry 'last_seen' '')
        $timesSeen = 0
        [void][int]::TryParse([string](Get-Field $entry 'times_seen' 0), [ref]$timesSeen)

        if (-not $merged.ContainsKey($name)) {
            $merged[$name] = [pscustomobject]@{
                notes          = New-Object System.Collections.Generic.List[string]
                last_character = $lastCharacter
                last_seen      = $lastSeen
                times_seen     = $timesSeen
            }
        }

        $target_entry = $merged[$name]

        # split on the join separator so re-merging an already merged file stays idempotent
        foreach ($part in ($note -split [regex]::Escape($NoteSeparator))) {
            $part = $part.Trim()
            if (-not [string]::IsNullOrWhiteSpace($part) -and -not $target_entry.notes.Contains($part)) {
                $target_entry.notes.Add($part)
            }
        }

        # timestamps are "YYYY-MM-DD HH:MM", so a plain string compare is chronological
        if ($lastSeen -gt $target_entry.last_seen) {
            $target_entry.last_seen = $lastSeen
            if (-not [string]::IsNullOrWhiteSpace($lastCharacter)) { $target_entry.last_character = $lastCharacter }
        }
        elseif ([string]::IsNullOrWhiteSpace($target_entry.last_character)) {
            $target_entry.last_character = $lastCharacter
        }

        if ($timesSeen -gt $target_entry.times_seen) { $target_entry.times_seen = $timesSeen }
    }

    Write-Host ("  {0,-40} {1} account(s)" -f (Split-Path $file -Leaf), $entryCount)
}

if ($merged.Count -eq 0) {
    Write-Warning 'Nothing to merge; squad_notes.json left untouched.'
    return
}

$out = [ordered]@{}
foreach ($name in ($merged.Keys | Sort-Object)) {
    $entry = $merged[$name]
    $out[$name] = [ordered]@{
        note           = ($entry.notes -join $NoteSeparator)
        last_character = $entry.last_character
        last_seen      = $entry.last_seen
        times_seen     = $entry.times_seen
    }
}

if (-not $NoBackup -and (Test-Path -LiteralPath $target -PathType Leaf)) {
    $backup = Join-Path $root ("squad_notes.backup-{0}.json" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
    Copy-Item -LiteralPath $target -Destination $backup -Force
    Write-Host "Backup written to $(Split-Path $backup -Leaf)"
}

$json = ($out | ConvertTo-Json -Depth 4)
[System.IO.File]::WriteAllText($target, $json + [Environment]::NewLine, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "Merged $fileCount file(s) into squad_notes.json ($($out.Count) accounts)." -ForegroundColor Green

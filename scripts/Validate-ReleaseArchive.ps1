param(
    [Parameter(Mandatory = $true)]
    [string]$ArchivePath
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
    throw "Release archive was not created: $ArchivePath"
}

$expectedEntries = @(
    "SKSE/Plugins/DragonbornPresence.dll"
    "SKSE/Plugins/DragonbornPresence.pdb"
    "SKSE/Plugins/discord_game_sdk.dll"
    "SKSE/Plugins/DragonbornPresence.json"
) | Sort-Object

Add-Type -AssemblyName System.IO.Compression.FileSystem
$resolvedArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
$archive = [System.IO.Compression.ZipFile]::OpenRead($resolvedArchivePath)

try {
    $fileEntries = @($archive.Entries | Where-Object { $_.Name.Length -gt 0 })
    $actualEntries = @($fileEntries | ForEach-Object {
        $_.FullName.Replace("\", "/")
    } | Sort-Object)

    $duplicateEntries = @($actualEntries | Group-Object | Where-Object { $_.Count -gt 1 })
    if ($duplicateEntries.Count -gt 0) {
        $names = ($duplicateEntries | ForEach-Object { $_.Name }) -join ", "
        throw "Release archive contains duplicate entries: $names"
    }

    $missingEntries = @($expectedEntries | Where-Object { $_ -notin $actualEntries })
    $unexpectedEntries = @($actualEntries | Where-Object { $_ -notin $expectedEntries })

    if ($missingEntries.Count -gt 0 -or $unexpectedEntries.Count -gt 0) {
        $problems = @()
        if ($missingEntries.Count -gt 0) {
            $problems += "missing: $($missingEntries -join ', ')"
        }
        if ($unexpectedEntries.Count -gt 0) {
            $problems += "unexpected: $($unexpectedEntries -join ', ')"
        }
        throw "Release archive manifest mismatch ($($problems -join '; '))."
    }

    foreach ($entry in $fileEntries) {
        if ($entry.Length -le 0) {
            throw "Release archive contains an empty file: $($entry.FullName)"
        }
    }

    $configEntry = $archive.GetEntry("SKSE/Plugins/DragonbornPresence.json")
    $reader = [System.IO.StreamReader]::new($configEntry.Open())
    try {
        $null = $reader.ReadToEnd() | ConvertFrom-Json
    }
    finally {
        $reader.Dispose()
    }
}
finally {
    $archive.Dispose()
}

$archiveHash = (Get-FileHash -LiteralPath $resolvedArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "Release archive manifest is valid."
Write-Host "SHA-256: $archiveHash"

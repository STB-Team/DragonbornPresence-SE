param(
    [Parameter(Mandatory = $true)]
    [string]$Tag,

    [string]$ProjectFile = "CMakeLists.txt"
)

$ErrorActionPreference = "Stop"

if ($Tag -notmatch '^v[0-9]+\.[0-9]+\.[0-9]+$') {
    throw "Release tag '$Tag' must use the exact vX.Y.Z format."
}

if (-not (Test-Path -LiteralPath $ProjectFile -PathType Leaf)) {
    throw "CMake project file was not found: $ProjectFile"
}

$projectContents = Get-Content -LiteralPath $ProjectFile -Raw
$versionMatch = [regex]::Match(
    $projectContents,
    'project\s*\(\s*DragonbornPresence\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)',
    [System.Text.RegularExpressions.RegexOptions]::IgnoreCase
)

if (-not $versionMatch.Success) {
    throw "The DragonbornPresence project version was not found in $ProjectFile."
}

$projectVersion = $versionMatch.Groups[1].Value
if ($Tag -ne "v$projectVersion") {
    throw "Release tag '$Tag' does not match project version '$projectVersion'."
}

Write-Host "Release tag '$Tag' matches project version '$projectVersion'."

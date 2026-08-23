$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$distRoot = (Resolve-Path (Join-Path $projectRoot 'dist')).Path
$portableRoot = Join-Path $distRoot 'HTDemucs GPU FX Portable'
$destination = Join-Path $distRoot 'HTDemucs_GPU_FX_Standalone_SelfContained_Windows_x64.zip'
$temporary = Join-Path $distRoot 'HTDemucs_GPU_FX_Standalone_SelfContained_Windows_x64.partial.zip'

if (-not (Test-Path -LiteralPath $portableRoot -PathType Container)) {
    throw "Portable folder not found: $portableRoot"
}
foreach ($path in @($destination, $temporary)) {
    if (-not ([System.IO.Path]::GetDirectoryName($path)).Equals(
            $distRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to write a ZIP outside the dist directory: $path"
    }
}

$textExtensions = @('.json', '.md', '.txt', '.xml')
$personalDataHits = Get-ChildItem -LiteralPath $portableRoot -Recurse -File |
    Where-Object { $_.Extension -in $textExtensions } |
    Select-String -Pattern '(?i)C:[\\/]+Users[\\/]+|/Users/' -List
if ($personalDataHits) {
    $details = ($personalDataHits | ForEach-Object { $_.Path }) -join [Environment]::NewLine
    throw "Refusing to package machine-identifying text:`n$details"
}

if (Test-Path -LiteralPath $temporary) {
    Remove-Item -LiteralPath $temporary -Force
}
Push-Location $distRoot
try {
    & "$env:SystemRoot\System32\tar.exe" --format zip -c -f $temporary `
        'HTDemucs GPU FX Portable'
    if ($LASTEXITCODE -ne 0) {
        throw "tar.exe failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}
if (-not (Test-Path -LiteralPath $temporary -PathType Leaf) -or
    (Get-Item -LiteralPath $temporary).Length -le 0) {
    throw 'The temporary ZIP was not created.'
}
Move-Item -LiteralPath $temporary -Destination $destination -Force

$hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
$bytes = (Get-Item -LiteralPath $destination).Length
Write-Output "windows_portable=$destination"
Write-Output "bytes=$bytes"
Write-Output "sha256=$hash"

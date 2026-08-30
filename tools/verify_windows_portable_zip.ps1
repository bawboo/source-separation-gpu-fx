$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = (Resolve-Path (Join-Path $projectRoot 'build')).Path
$zipPath = Join-Path $projectRoot 'dist\HTDemucs_GPU_FX_Standalone_SelfContained_Windows_x64.zip'
$extractRoot = Join-Path $buildRoot 'windows-portable-zip-verification'
$portableRoot = Join-Path $extractRoot 'Music SSP FX Portable'

if (-not (Test-Path -LiteralPath $zipPath -PathType Leaf)) {
    throw "Windows portable ZIP not found: $zipPath"
}
if (-not (Split-Path -Parent $extractRoot).Equals(
        $buildRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to replace an extraction folder outside build: $extractRoot"
}
if (Test-Path -LiteralPath $extractRoot) {
    Remove-Item -LiteralPath $extractRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $extractRoot -Force | Out-Null

try {
    & "$env:SystemRoot\System32\tar.exe" -xf $zipPath -C $extractRoot
    if ($LASTEXITCODE -ne 0) {
        throw "tar.exe extraction failed with exit code $LASTEXITCODE"
    }
    if (-not (Test-Path -LiteralPath $portableRoot -PathType Container)) {
        throw 'The expected portable folder is missing after extraction.'
    }
    & (Join-Path $PSScriptRoot 'verify_standalone_cleanroom.ps1') `
        -PortableRoot $portableRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Clean-room verifier failed with exit code $LASTEXITCODE"
    }
    $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Output 'zip_extraction=PASS'
    Write-Output "zip_sha256=$hash"
} finally {
    if (Test-Path -LiteralPath $extractRoot) {
        Remove-Item -LiteralPath $extractRoot -Recurse -Force
    }
}

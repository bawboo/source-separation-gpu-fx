param(
    [string]$BuildDirectory = '',
    [string]$Generator = 'Visual Studio 17 2022'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectRoot 'build\windows-installed'
}

& (Join-Path $PSScriptRoot 'apply_dependency_patches.ps1')
if ($LASTEXITCODE -ne 0) {
    throw "Dependency patching failed with exit code $LASTEXITCODE"
}

cmake -S $projectRoot -B $BuildDirectory -G $Generator -A x64 `
    -DHTFX_BUILD_PLUGIN=ON `
    -DHTFX_STANDALONE_ONLY=ON `
    -DHTFX_PORTABLE_STANDALONE_SETTINGS=OFF
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}

cmake --build $BuildDirectory --config Release --target `
    HTDemucsGpuFX_Standalone htfx_hardware_probe -- /m
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed with exit code $LASTEXITCODE"
}

$standalone = Join-Path $BuildDirectory `
    'HTDemucsGpuFX_artefacts\Release\Standalone\HTDemucs GPU FX.exe'
$probe = Join-Path $BuildDirectory 'Release\htfx_hardware_probe.exe'
foreach ($required in @($standalone, $probe)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Expected installed-build artifact was not created: $required"
    }
}

$probeReport = Join-Path $BuildDirectory 'hardware-probe.json'
& $probe --json $probeReport
if ($LASTEXITCODE -ne 0) {
    throw "Hardware probe failed with exit code $LASTEXITCODE"
}
Get-Content -LiteralPath $probeReport -Raw -Encoding utf8 | ConvertFrom-Json | Out-Null

Write-Output "standalone=$standalone"
Write-Output "hardware_probe=$probe"
Write-Output "probe_report=$probeReport"

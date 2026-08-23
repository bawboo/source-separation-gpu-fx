param(
    [string]$Version = '0.1.0',
    [string]$BuildDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectRoot 'build\windows-installed'
}
$standalone = Join-Path $BuildDirectory `
    'HTDemucsGpuFX_artefacts\Release\Standalone\HTDemucs GPU FX.exe'
$hardwareProbe = Join-Path $BuildDirectory 'Release\htfx_hardware_probe.exe'
$installerIcon = Join-Path $projectRoot 'dist\HTDemucs GPU FX.vst3\Plugin.ico'
$payloadRoot = Join-Path $projectRoot 'build\windows-web\payload'
$sidecarRoot = Join-Path $payloadRoot 'Resources\sidecar'

foreach ($required in @($standalone, $hardwareProbe, $installerIcon)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required installed-build component not found: $required"
    }
}

function Copy-FilteredTree {
    param([string]$Source, [string]$Destination)
    $sourceResolved = (Resolve-Path -LiteralPath $Source).Path
    Get-ChildItem -LiteralPath $sourceResolved -Recurse -File | Where-Object {
        $_.FullName -notmatch '[\\/]\.git[\\/]' -and
        $_.FullName -notmatch '[\\/]__pycache__[\\/]' -and
        $_.Extension -ne '.pyc'
    } | ForEach-Object {
        $relative = $_.FullName.Substring($sourceResolved.Length).TrimStart('\','/')
        $target = Join-Path $Destination $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $target
    }
}

$stagingParent = Split-Path -Parent $payloadRoot
New-Item -ItemType Directory -Path $stagingParent -Force | Out-Null
if (Test-Path -LiteralPath $payloadRoot) {
    $resolvedPayload = (Resolve-Path -LiteralPath $payloadRoot).Path
    $resolvedParent = (Resolve-Path -LiteralPath $stagingParent).Path
    if (-not $resolvedPayload.StartsWith($resolvedParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace payload outside $resolvedParent"
    }
    Remove-Item -LiteralPath $resolvedPayload -Recurse -Force
}
New-Item -ItemType Directory -Path $sidecarRoot -Force | Out-Null
Copy-Item -LiteralPath $standalone -Destination (Join-Path $payloadRoot 'HTDemucs GPU FX.exe')
Copy-Item -LiteralPath $hardwareProbe -Destination (Join-Path $payloadRoot 'htfx_hardware_probe.exe')
Copy-Item -LiteralPath $installerIcon -Destination (Join-Path $payloadRoot 'HTDemucs GPU FX.ico')

$trees = @(
    @{ Source = (Join-Path $projectRoot 'worker'); Destination = (Join-Path $sidecarRoot 'worker') },
    @{ Source = (Join-Path $projectRoot 'src\htdemucs_gpu_fx'); Destination = (Join-Path $sidecarRoot 'src\htdemucs_gpu_fx') },
    @{ Source = (Join-Path $projectRoot 'third_party\demucs\demucs'); Destination = (Join-Path $sidecarRoot 'demucs_repo\demucs') }
)
foreach ($tree in $trees) {
    Copy-FilteredTree -Source $tree.Source -Destination $tree.Destination
}

$modelMetadata = Join-Path $sidecarRoot 'models'
New-Item -ItemType Directory -Path $modelMetadata -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets\models\model-manifest.json') `
    -Destination $modelMetadata
Get-ChildItem -LiteralPath (Join-Path $projectRoot 'assets\models') -Filter '*.yaml' -File |
    Copy-Item -Destination $modelMetadata

$portableLicenses = Join-Path $projectRoot 'dist\HTDemucs GPU FX Portable\Licenses'
$licensesDestination = Join-Path $payloadRoot 'Licenses'
if (-not (Test-Path -LiteralPath $portableLicenses -PathType Container)) {
    throw "The verified licence bundle is missing: $portableLicenses"
}
Copy-FilteredTree -Source $portableLicenses -Destination $licensesDestination
Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md') `
    -Destination (Join-Path $licensesDestination 'THIRD_PARTY_NOTICES.md') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE.md') `
    -Destination (Join-Path $licensesDestination 'HTFX-LICENSE.md') -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'COPYING') `
    -Destination (Join-Path $licensesDestination 'AGPL-3.0.txt') -Force

Get-ChildItem -LiteralPath $payloadRoot -Recurse -Filter '*.th' -File | ForEach-Object {
    throw "Installer payload must not contain model weights: $($_.FullName)"
}
$personalDataHits = Get-ChildItem -LiteralPath $payloadRoot -Recurse -File |
    Where-Object { $_.Extension -in @('.json', '.md', '.txt', '.xml', '.yaml') } |
    Select-String -Pattern '(?i)C:[\\/]+Users[\\/]+|/Users/' -List
if ($personalDataHits) {
    $details = ($personalDataHits | ForEach-Object Path) -join [Environment]::NewLine
    throw "Refusing to stage machine-identifying text:`n$details"
}

$manifest = [ordered]@{
    schema_version = 1
    product = 'HTDemucs GPU FX'
    version = $Version
    architecture = 'windows-x64'
    payload_kind = 'web-installer-base'
    executable = 'HTDemucs GPU FX.exe'
    executable_sha256 = (Get-FileHash -LiteralPath `
        (Join-Path $payloadRoot 'HTDemucs GPU FX.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    hardware_probe = 'htfx_hardware_probe.exe'
    hardware_probe_sha256 = (Get-FileHash -LiteralPath `
        (Join-Path $payloadRoot 'htfx_hardware_probe.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    contains_runtime = $false
    contains_model_weights = $false
    model_metadata = 'Resources/sidecar/models/model-manifest.json'
    settings_mode = 'per-user-local-app-data'
}
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $payloadRoot 'installer-payload-manifest.json') -Encoding utf8

Write-Output "payload=$payloadRoot"
Write-Output "bytes=$((Get-ChildItem -LiteralPath $payloadRoot -Recurse -File | Measure-Object Length -Sum).Sum)"
Write-Output "executable_sha256=$($manifest.executable_sha256)"

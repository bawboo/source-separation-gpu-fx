param(
    [string]$Version = '0.1.0',
    [string]$BuildDirectory = '',
    # python.exe of the environment used to freeze the runtime being shipped.
    [string]$LicenseCollectorPython = '',
    # The app only shells out to FFmpeg to decode into PCM, so the LGPL build
    # covers every use and keeps binary releases clear of the GPL
    # corresponding-source obligation. See build/ffmpeg-lgpl/BUILD_INFO.txt.
    [string]$Ffmpeg = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($Ffmpeg)) {
    $Ffmpeg = Join-Path $projectRoot 'build\ffmpeg-lgpl\bin\ffmpeg.exe'
}
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $projectRoot 'build\windows-installed'
}
$standalone = Join-Path $BuildDirectory `
    'HTDemucsGpuFX_artefacts\Release\Standalone\Music SSP FX.exe'
$hardwareProbe = Join-Path $BuildDirectory 'Release\htfx_hardware_probe.exe'
# JUCE generates the application icon during the build; prefer it over the
# packaged VST3 bundle, which only exists after a separate packaging step.
$installerIcon = Join-Path $BuildDirectory `
    'HTDemucsGpuFX_artefacts\JuceLibraryCode\icon.ico'
if (-not (Test-Path -LiteralPath $installerIcon -PathType Leaf)) {
    $installerIcon = Join-Path $projectRoot 'dist\Music SSP FX.vst3\Plugin.ico'
}
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
Copy-Item -LiteralPath $standalone -Destination (Join-Path $payloadRoot 'Music SSP FX.exe')
Copy-Item -LiteralPath $hardwareProbe -Destination (Join-Path $payloadRoot 'htfx_hardware_probe.exe')
Copy-Item -LiteralPath $installerIcon -Destination (Join-Path $payloadRoot 'Music SSP FX.ico')

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
# Both manifests are required: without roformer-manifest.json the mode list
# degrades to HTDemucs 4/6-stem only and all 99 RoFormer models disappear.
foreach ($manifest in @('model-manifest.json', 'roformer-manifest.json')) {
    $source = Join-Path $projectRoot "assets\models\$manifest"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required model manifest is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination $modelMetadata
}
Get-ChildItem -LiteralPath (Join-Path $projectRoot 'assets\models') -Filter '*.yaml' -File |
    Copy-Item -Destination $modelMetadata

# Notices must match the exact distributions frozen into the runtime, so they
# are collected from the interpreter that froze it instead of copied from a
# previously packaged tree with its own (possibly stale) versions.
$licensesDestination = Join-Path $payloadRoot 'Licenses'
New-Item -ItemType Directory -Path $licensesDestination -Force | Out-Null
if ([string]::IsNullOrWhiteSpace($LicenseCollectorPython)) {
    throw 'Pass -LicenseCollectorPython <python.exe of the runtime environment>.'
}
& $LicenseCollectorPython (Join-Path $projectRoot 'tools\collect_runtime_licenses.py') `
    --destination (Join-Path $licensesDestination 'python-packages') `
    --report (Join-Path $licensesDestination 'python-packages\collection-report.json')
if ($LASTEXITCODE -ne 0) {
    throw 'Collecting the runtime package licences failed.'
}
$staticNotices = @(
    @{ Source = (Join-Path $projectRoot 'third_party\JUCE\LICENSE.md'); Name = 'JUCE-LICENSE.md' },
    @{ Source = (Join-Path $projectRoot 'third_party\demucs\LICENSE'); Name = 'DEMUCS-LICENSE.txt' },
    @{ Source = (Join-Path $projectRoot 'packaging\NVIDIA-CUDA-NOTICE.txt'); Name = 'NVIDIA-CUDA-NOTICE.txt' }
)
foreach ($notice in $staticNotices) {
    if (-not (Test-Path -LiteralPath $notice.Source -PathType Leaf)) {
        throw "Required licence file is missing: $($notice.Source)"
    }
    Copy-Item -LiteralPath $notice.Source `
        -Destination (Join-Path $licensesDestination $notice.Name) -Force
}
# The LGPL requires naming where the exact binary came from, so its licence and
# build record both ship next to the notices.
$ffmpegRoot = Split-Path -Parent (Split-Path -Parent $Ffmpeg)
foreach ($pair in @(
    @{ File = 'LICENSE.txt';    Name = 'FFMPEG-LICENSE.txt' },
    @{ File = 'BUILD_INFO.txt'; Name = 'FFMPEG-BUILD_INFO.txt' })) {
    $source = Join-Path $ffmpegRoot $pair.File
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "FFmpeg $($pair.File) not found next to the bundled binary: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $licensesDestination $pair.Name) -Force
}
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
    product = 'Music SSP FX'
    version = $Version
    architecture = 'windows-x64'
    payload_kind = 'web-installer-base'
    executable = 'Music SSP FX.exe'
    executable_sha256 = (Get-FileHash -LiteralPath `
        (Join-Path $payloadRoot 'Music SSP FX.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
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

param(
    [string]$Version = '0.1.0'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$distRoot = Join-Path $projectRoot 'dist\windows-web'
$payloadRoot = Join-Path $projectRoot 'build\windows-web\payload'
$modelManifestPath = Join-Path $projectRoot 'assets\models\model-manifest.json'
$modelManifest = Get-Content -LiteralPath $modelManifestPath -Raw -Encoding utf8 |
    ConvertFrom-Json

if ($modelManifest.registry_version -lt 2) {
    throw 'Model registry must include official download metadata.'
}
foreach ($model in $modelManifest.models.PSObject.Properties) {
    foreach ($file in $model.Value.files) {
        $artifact = $modelManifest.artifacts.$file
        if ($null -eq $artifact) {
            throw "Missing artifact metadata for $file"
        }
        $uri = [Uri]$artifact.url
        if ($uri.Scheme -ne 'https' -or
            $uri.Host -notin @($modelManifest.download_host_allowlist)) {
            throw "Model artifact does not use an allowlisted HTTPS host: $($artifact.url)"
        }
        if ($artifact.sha256 -ne $modelManifest.sha256.$file) {
            throw "Model SHA-256 tables disagree for $file"
        }
    }
}

$payloadWeights = Get-ChildItem -LiteralPath $payloadRoot -Recurse -Filter '*.th' -File
if ($payloadWeights) {
    throw "Base installer payload contains model weights: $($payloadWeights.FullName -join ', ')"
}

$releasePackages = [ordered]@{}
$githubReleaseAssetLimit = 2GB
foreach ($flavor in @('cpu', 'cuda')) {
    $manifestPath = Join-Path $distRoot "runtime-win-x64-$flavor-$Version.json"
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding utf8 |
        ConvertFrom-Json
    $archives = @($manifest.archives)
    $expectedArchiveCount = if ($flavor -eq 'cpu') { 1 } else { 2 }
    if ($archives.Count -ne $expectedArchiveCount) {
        throw "$flavor runtime must contain $expectedArchiveCount release archive(s)"
    }
    if ($manifest.worker_manifest.flavor -ne $flavor) {
        throw "$flavor runtime contains the wrong worker flavor"
    }
    if ($flavor -eq 'cpu' -and $null -ne $manifest.worker_manifest.versions.cuda) {
        throw 'CPU runtime unexpectedly contains a CUDA PyTorch build'
    }
    if ($flavor -eq 'cuda' -and
        [string]::IsNullOrWhiteSpace($manifest.worker_manifest.versions.cuda)) {
        throw 'CUDA runtime does not report a CUDA PyTorch build'
    }

    $allNames = @()
    $releaseAssets = @()
    foreach ($archiveMetadata in $archives) {
        $archivePath = Join-Path $distRoot $archiveMetadata.archive
        $actualBytes = (Get-Item -LiteralPath $archivePath).Length
        if ($actualBytes -ne $archiveMetadata.bytes) {
            throw "$flavor runtime size mismatch: $($archiveMetadata.archive)"
        }
        if ($actualBytes -ge $githubReleaseAssetLimit) {
            throw "$($archiveMetadata.archive) is too large for a GitHub Release asset"
        }
        $hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $archiveMetadata.sha256) {
            throw "$flavor runtime SHA-256 mismatch: $($archiveMetadata.archive)"
        }

        $archive = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
        try {
            $names = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\','/') })
            if ($names | Where-Object {
                $_.StartsWith('/') -or $_ -match '(^|/)\.\.(/|$)'
            }) {
                throw "$flavor runtime archive contains an unsafe path"
            }
            if ($names | Where-Object { $_.EndsWith('.th') }) {
                throw "$flavor runtime archive contains model weights"
            }
            $duplicates = @($names | Where-Object { $_ -in $allNames })
            if ($duplicates) {
                throw "$flavor runtime archives contain duplicate paths: $($duplicates -join ', ')"
            }
            $allNames += $names
        } finally {
            $archive.Dispose()
        }
        $releaseAssets += [ordered]@{
            file = $archiveMetadata.archive
            role = $archiveMetadata.role
            bytes = [int64]$archiveMetadata.bytes
            sha256 = $archiveMetadata.sha256
        }
    }

    foreach ($requiredSuffix in @(
        'Resources/sidecar/Runtime/htdemucs-worker/htdemucs-worker.exe',
        'Resources/sidecar/Runtime/ffmpeg/bin/ffmpeg.exe',
        'Resources/sidecar/Runtime/ffmpeg/bin/ffprobe.exe')) {
        if ($requiredSuffix -notin $allNames) {
            throw "$flavor runtime archives are missing $requiredSuffix"
        }
    }
    $releasePackages[$flavor] = [ordered]@{
        assets = $releaseAssets
        total_bytes = [int64](($releaseAssets | ForEach-Object {
            [int64]$_['bytes']
        } | Measure-Object -Sum).Sum)
        torch = $manifest.worker_manifest.versions.torch
        cuda = $manifest.worker_manifest.versions.cuda
    }
}

$defaultModel = $modelManifest.models.$($modelManifest.default_model)
$defaultFile = $defaultModel.files[0]
$defaultArtifact = $modelManifest.artifacts.$defaultFile
$releaseManifest = [ordered]@{
    schema_version = 1
    product = 'HTDemucs GPU FX'
    version = $Version
    architecture = 'windows-x64'
    setup_kind = 'web-installer'
    runtime_packages = $releasePackages
    default_model = [ordered]@{
        id = $modelManifest.default_model
        file = $defaultFile
        bytes = [int64]$defaultArtifact.bytes
        sha256 = $defaultArtifact.sha256
        url = $defaultArtifact.url
    }
    payload_bytes = [int64](
        Get-ChildItem -LiteralPath $payloadRoot -Recurse -File |
        Measure-Object Length -Sum).Sum
    contains_model_weights = $false
}
$releaseManifestPath = Join-Path $distRoot 'release-manifest.json'
$releaseManifest | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $releaseManifestPath -Encoding utf8

Write-Output 'windows_web_packages=PASS'
Write-Output "release_manifest=$releaseManifestPath"
Write-Output "cpu_bytes=$($releasePackages.cpu.total_bytes)"
Write-Output "cuda_bytes=$($releasePackages.cuda.total_bytes)"
Write-Output "payload_bytes=$($releaseManifest.payload_bytes)"

param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('cuda', 'cpu')]
    [string]$Flavor,
    [string]$Version = '0.1.0',
    [string]$Ffmpeg = 'C:\ffmpeg-master\bin\ffmpeg.exe'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Join-Path $projectRoot 'build\windows-web'
$distRoot = Join-Path $projectRoot 'dist\windows-web'
$runtimeBuildName = if ($Flavor -eq 'cuda') {
    'standalone-runtime-dist'
} else {
    'standalone-runtime-cpu-dist'
}
$workerSource = Join-Path $projectRoot "build\$runtimeBuildName\htdemucs-worker"
$workerExecutable = Join-Path $workerSource 'htdemucs-worker.exe'
foreach ($required in @($workerExecutable, $Ffmpeg)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required runtime component not found: $required"
    }
}

$stageRoot = Join-Path $buildRoot "runtime-$Flavor"
$sidecarRoot = Join-Path $stageRoot 'Resources\sidecar'
$runtimeRoot = Join-Path $sidecarRoot 'Runtime'
if (Test-Path -LiteralPath $stageRoot) {
    $resolved = (Resolve-Path -LiteralPath $stageRoot).Path
    $resolvedBuild = (Resolve-Path -LiteralPath $buildRoot).Path
    if (-not $resolved.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace runtime staging outside $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Path $runtimeRoot,$distRoot -Force | Out-Null
Copy-Item -LiteralPath $workerSource `
    -Destination (Join-Path $runtimeRoot 'htdemucs-worker') -Recurse

$ffmpegSourceRoot = Split-Path -Parent (Resolve-Path -LiteralPath $Ffmpeg).Path
$ffmpegDestination = Join-Path $runtimeRoot 'ffmpeg\bin'
New-Item -ItemType Directory -Path $ffmpegDestination -Force | Out-Null
foreach ($name in @('ffmpeg.exe', 'ffprobe.exe')) {
    $source = Join-Path $ffmpegSourceRoot $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required FFmpeg executable not found: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $ffmpegDestination $name)
}
Get-ChildItem -LiteralPath $ffmpegSourceRoot -Filter '*.dll' -File |
    Copy-Item -Destination $ffmpegDestination

$archiveSpecs = @()
if ($Flavor -eq 'cpu') {
    $archiveSpecs = @(
        [ordered]@{
            name = "runtime-win-x64-cpu-$Version.zip"
            role = 'runtime'
            files = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse)
        }
    )
} else {
    # GitHub Release assets must each remain below 2 GiB. Keep the three
    # largest CUDA DLLs in a second archive while preserving their paths so
    # Inno Setup can extract both archives into the same application folder.
    $cudaLibraryNames = @(
        'torch_cuda.dll',
        'cudnn_cnn_infer64_8.dll',
        'cublasLt64_12.dll'
    )
    $allFiles = @(Get-ChildItem -LiteralPath $stageRoot -File -Recurse)
    $libraryFiles = @($allFiles | Where-Object { $_.Name -in $cudaLibraryNames })
    if ($libraryFiles.Count -ne $cudaLibraryNames.Count) {
        throw 'The expected large CUDA runtime libraries were not found.'
    }
    $coreFiles = @($allFiles | Where-Object { $_.Name -notin $cudaLibraryNames })
    $archiveSpecs = @(
        [ordered]@{
            name = "runtime-win-x64-cuda-core-$Version.zip"
            role = 'core'
            files = $coreFiles
        },
        [ordered]@{
            name = "runtime-win-x64-cuda-libraries-$Version.zip"
            role = 'cuda-libraries'
            files = $libraryFiles
        }
    )
}

$archiveManifests = @()
$archiveListRoot = Join-Path $buildRoot 'archive-lists'
New-Item -ItemType Directory -Path $archiveListRoot -Force | Out-Null
foreach ($spec in $archiveSpecs) {
    $archivePath = Join-Path $distRoot $spec.name
    $partialPath = "$archivePath.partial"
    foreach ($path in @($archivePath, $partialPath)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force
        }
    }

    $listPath = Join-Path $archiveListRoot "$($spec.role)-$Flavor-$Version.txt"
    @($spec.files | ForEach-Object {
        $_.FullName.Substring($stageRoot.Length + 1).Replace('\', '/')
    }) | Set-Content -LiteralPath $listPath -Encoding ascii

    Push-Location $stageRoot
    try {
        & "$env:SystemRoot\System32\tar.exe" --format zip -c -f $partialPath -T $listPath
        if ($LASTEXITCODE -ne 0) {
            throw "tar.exe failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
    Move-Item -LiteralPath $partialPath -Destination $archivePath
    $archiveManifests += [ordered]@{
        archive = $spec.name
        role = $spec.role
        bytes = (Get-Item -LiteralPath $archivePath).Length
        sha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$manifest = [ordered]@{
    schema_version = 1
    flavor = $Flavor
    version = $Version
    architecture = 'windows-x64'
    archives = $archiveManifests
    total_bytes = [int64](($archiveManifests | ForEach-Object {
        [int64]$_['bytes']
    } | Measure-Object -Sum).Sum)
    extract_root = 'Resources/sidecar'
    worker_manifest = Get-Content -LiteralPath `
        (Join-Path $workerSource 'runtime-manifest.json') -Raw -Encoding utf8 |
        ConvertFrom-Json
    ffmpeg_sha256 = (Get-FileHash -LiteralPath `
        (Join-Path $ffmpegDestination 'ffmpeg.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
}
$manifestPath = Join-Path $distRoot "runtime-win-x64-$Flavor-$Version.json"
$manifest | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $manifestPath -Encoding utf8

foreach ($archive in $archiveManifests) {
    Write-Output "runtime_archive=$(Join-Path $distRoot $archive.archive)"
    Write-Output "bytes=$($archive.bytes)"
    Write-Output "sha256=$($archive.sha256)"
}
Write-Output "runtime_manifest=$manifestPath"
Write-Output "total_bytes=$($manifest.total_bytes)"

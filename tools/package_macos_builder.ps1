param(
    [string]$Destination = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$workspaceRoot = (Resolve-Path (Join-Path $projectRoot '..\..')).Path
$distRoot = Join-Path $projectRoot 'dist'
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $distRoot 'HTDemucs_GPU_FX_macOS_Builder_Source.zip'
}
$stageRoot = Join-Path $projectRoot 'build\macos-builder-stage'
$bundleRoot = Join-Path $stageRoot 'HTDemucs_GPU_FX_macOS_Builder'
$projectDestination = Join-Path $bundleRoot 'outputs\htdemucs_gpu_fx'
$demucsDestination = Join-Path $bundleRoot 'work\demucs'

function Copy-FilteredTree {
    param(
        [Parameter(Mandatory)] [string]$Source,
        [Parameter(Mandatory)] [string]$Target,
        [string[]]$ExcludedTopLevel = @()
    )
    $sourceResolved = (Resolve-Path -LiteralPath $Source).Path
    Get-ChildItem -LiteralPath $sourceResolved -Recurse -File | Where-Object {
        $relative = $_.FullName.Substring($sourceResolved.Length).TrimStart('\','/')
        $topLevel = ($relative -split '[\\/]')[0]
        $requiredMacTool = $topLevel -ne 'tools' -or
            $relative -match '^tools[\\/](macos|pyinstaller-hooks)[\\/]'
        $topLevel -notin $ExcludedTopLevel -and
        $requiredMacTool -and
        $relative -ne 'README.md' -and
        $_.Name -notlike 'claude-review-*.txt' -and
        $_.FullName -notmatch '[\\/]\.git[\\/]' -and
        $_.FullName -notmatch '[\\/]__pycache__[\\/]' -and
        $_.FullName -notmatch '[\\/]\.pytest_cache[\\/]' -and
        $_.Extension -notin @('.pyc', '.pdb', '.ilk', '.zip') -and
        $_.Name -ne 'reaper778_x64-install.exe'
    } | ForEach-Object {
        $relative = $_.FullName.Substring($sourceResolved.Length).TrimStart('\','/')
        $targetFile = Join-Path $Target $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $targetFile) -Force | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $targetFile
    }
}

$stageParent = Split-Path -Parent $stageRoot
if (-not (Resolve-Path -LiteralPath $stageParent).Path.Equals(
        (Resolve-Path -LiteralPath (Join-Path $projectRoot 'build')).Path,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to replace a stage outside the project build directory.'
}
if (Test-Path -LiteralPath $stageRoot) {
    Remove-Item -LiteralPath $stageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $projectDestination,$demucsDestination,$distRoot -Force | Out-Null

Copy-FilteredTree -Source $projectRoot -Target $projectDestination `
    -ExcludedTopLevel @('build', 'dist', 'results')
Copy-FilteredTree -Source (Join-Path $workspaceRoot 'work\demucs\demucs') `
    -Target (Join-Path $demucsDestination 'demucs')
Copy-Item -LiteralPath (Join-Path $workspaceRoot 'work\demucs\LICENSE') `
    -Destination (Join-Path $demucsDestination 'LICENSE')

$textExtensions = @(
    '.cmake', '.command', '.cpp', '.h', '.hpp', '.json', '.md', '.ps1',
    '.py', '.sh', '.txt', '.xml', '.yaml', '.yml'
)
$personalDataHits = Get-ChildItem -LiteralPath $bundleRoot -Recurse -File |
    Where-Object {
        $_.FullName -notmatch '[\\/]third_party[\\/]' -and
        $_.FullName -notmatch '[\\/]work[\\/]demucs[\\/]' -and
        ($_.Extension -in $textExtensions -or $_.Name -eq 'CMakeLists.txt')
    } |
    Select-String -Pattern '(?i)C:[\\/]+Users[\\/]+|/Users/' -List
if ($personalDataHits) {
    $details = ($personalDataHits | ForEach-Object { $_.Path }) -join [Environment]::NewLine
    throw "Refusing to package text files containing machine-identifying paths:`n$details"
}

$copiedProjectRoot = $projectDestination
$modelManifestPath = Join-Path $copiedProjectRoot 'assets\models\model-manifest.json'
$modelRegistry = Get-Content -LiteralPath $modelManifestPath -Raw -Encoding utf8 |
    ConvertFrom-Json
$verifiedModelHashes = [ordered]@{}
foreach ($entry in $modelRegistry.sha256.PSObject.Properties) {
    $modelFile = Join-Path $copiedProjectRoot (Join-Path 'assets\models' $entry.Name)
    if (-not (Test-Path -LiteralPath $modelFile -PathType Leaf)) {
        throw "Copied Mac builder model is missing: $modelFile"
    }
    $actual = (Get-FileHash -LiteralPath $modelFile -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $entry.Value) {
        throw "Copied Mac builder model checksum mismatch for $($entry.Name)"
    }
    $verifiedModelHashes[$entry.Name] = $actual
}

$keyRelativePaths = @(
    'CMakeLists.txt',
    'cpp\GpuWorkerClientPosix.cpp',
    'worker\gpu_ipc_worker.py',
    'worker\ipc_worker_posix.py',
    'tools\macos\build_macos_portable.sh',
    'tools\macos\verify_portable.command',
    'tests\worker_registry_smoke_posix.cpp',
    'MACOS_PORTABLE_BUILD.md'
)
$keyHashes = [ordered]@{}
foreach ($relative in $keyRelativePaths) {
    $keyFile = Join-Path $copiedProjectRoot $relative
    if (-not (Test-Path -LiteralPath $keyFile -PathType Leaf)) {
        throw "Key Mac builder source is missing: $relative"
    }
    $portableRelative = $relative.Replace('\', '/')
    $keyHashes[$portableRelative] =
        (Get-FileHash -LiteralPath $keyFile -Algorithm SHA256).Hash.ToLowerInvariant()
}
$payloadFiles = @(Get-ChildItem -LiteralPath $bundleRoot -Recurse -File)
$sourceManifest = [ordered]@{
    product = 'Music SSP FX macOS Builder Source'
    version = '0.1.0-local-test'
    targets = @(
        [ordered]@{ architecture = 'arm64'; minimum_macos = '12.3'; default_compute = 'mps' },
        [ordered]@{ architecture = 'x86_64'; minimum_macos = '10.15'; default_compute = 'cpu' }
    )
    runtime_pins = [ordered]@{
        python = '3.11'
        torch = '2.1.2'
        numpy = '1.26.4'
        pyinstaller = '6.16.0'
        imageio_ffmpeg = '0.6.0'
    }
    model_count = @($modelRegistry.models.PSObject.Properties).Count
    model_file_count = @($modelRegistry.sha256.PSObject.Properties).Count
    model_sha256 = $verifiedModelHashes
    key_file_sha256 = $keyHashes
    payload_file_count_excluding_manifest = $payloadFiles.Count
    payload_bytes_excluding_manifest = ($payloadFiles | Measure-Object Length -Sum).Sum
    machine_identifying_text_scan = 'PASS'
    external_runtime_download_required_after_build = $false
    generated_utc = [DateTime]::UtcNow.ToString('o')
}
$sourceManifest | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $bundleRoot 'builder-source-manifest.json') `
        -Encoding utf8

$instructions = @'
Music SSP FX macOS builder source
=====================================

1. Move this whole folder to the Mac; do not separate outputs and work.
2. Open outputs/htdemucs_gpu_fx/MACOS_PORTABLE_BUILD.md.
3. Build Apple Silicon first with tools/macos/build_macos_portable.sh.
4. Return verification-result.txt and verification-details.log.

builder-source-manifest.json records the verified model and key-source hashes.

The build downloads pinned build dependencies. The final portable app is
self-contained and does not need Python, PyTorch, FFmpeg, Homebrew, or CMake.
'@
Set-Content -LiteralPath (Join-Path $bundleRoot 'BUILD_ON_MAC_FIRST.txt') `
    -Value $instructions -Encoding utf8

$Destination = [System.IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $Destination) {
    Remove-Item -LiteralPath $Destination -Force
}
Push-Location $stageRoot
try {
    & "$env:SystemRoot\System32\tar.exe" -a -c -f $Destination `
        'HTDemucs_GPU_FX_macOS_Builder'
    if ($LASTEXITCODE -ne 0) {
        throw "tar.exe failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

$hash = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLowerInvariant()
$bytes = (Get-Item -LiteralPath $Destination).Length
Write-Output "builder_source=$Destination"
Write-Output "bytes=$bytes"
Write-Output "sha256=$hash"

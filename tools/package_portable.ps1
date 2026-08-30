param(
    [ValidateSet('cuda', 'cpu')]
    [string]$Flavor = 'cuda',
    [string]$Version = '0.0.2',
    [string]$Ffmpeg = 'C:\ffmpeg-master\bin\ffmpeg.exe',
    # GitHub caps a single release asset at 2 GiB, so a package larger than this
    # is split into volumes that all extract into the same folder.
    [long]$MaxArchiveBytes = 1782579200
)

# Builds the no-install portable package. It is assembled from exactly the same
# pieces the web installer ships - the installer payload plus one runtime - so
# the portable and installed layouts cannot drift apart.

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$payloadRoot = Join-Path $projectRoot 'build\windows-web\payload'
$runtimeDist = if ($Flavor -eq 'cuda') {
    Join-Path $projectRoot 'build\standalone-runtime-dist\htdemucs-worker'
} else {
    Join-Path $projectRoot 'build\standalone-runtime-cpu-dist\htdemucs-worker'
}
$distRoot = Join-Path $projectRoot 'dist\portable'
$stageRoot = Join-Path $projectRoot "build\portable-$Flavor"
$baseName = "Music_SSP_FX_Portable_win64_$Flavor-$Version"

foreach ($required in @(
    (Join-Path $payloadRoot 'Music SSP FX.exe'),
    (Join-Path $runtimeDist 'htdemucs-worker.exe'),
    $Ffmpeg)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required component not found: $required"
    }
}

New-Item -ItemType Directory -Path $distRoot -Force | Out-Null
foreach ($stale in @(Get-ChildItem -LiteralPath $distRoot -Filter "$baseName*" -File -ErrorAction SilentlyContinue)) {
    Remove-Item -LiteralPath $stale.FullName -Force
}
if (Test-Path -LiteralPath $stageRoot) {
    $resolved = (Resolve-Path -LiteralPath $stageRoot).Path
    if (-not $resolved.StartsWith($projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside the project: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null

Write-Host 'Staging the installer payload...'
# -LiteralPath would treat the wildcard as a literal name and silently copy
# nothing but the directories, leaving the executable behind.
Copy-Item -Path (Join-Path $payloadRoot '*') -Destination $stageRoot -Recurse -Force
# The hardware probe only exists to pick a runtime during setup; a portable
# package already contains the one it ships with.
Remove-Item -LiteralPath (Join-Path $stageRoot 'htfx_hardware_probe.exe') -Force -ErrorAction SilentlyContinue

$runtimeRoot = Join-Path $stageRoot 'Resources\sidecar\Runtime'
New-Item -ItemType Directory -Path $runtimeRoot -Force | Out-Null
Write-Host "Staging the $Flavor runtime (this copies several GB for cuda)..."
Copy-Item -LiteralPath $runtimeDist -Destination (Join-Path $runtimeRoot 'htdemucs-worker') -Recurse

$ffmpegSource = Split-Path -Parent (Resolve-Path -LiteralPath $Ffmpeg).Path
$ffmpegDestination = Join-Path $runtimeRoot 'ffmpeg\bin'
New-Item -ItemType Directory -Path $ffmpegDestination -Force | Out-Null
foreach ($name in @('ffmpeg.exe', 'ffprobe.exe')) {
    Copy-Item -LiteralPath (Join-Path $ffmpegSource $name) -Destination $ffmpegDestination
}
Get-ChildItem -LiteralPath $ffmpegSource -Filter '*.dll' -File |
    Copy-Item -Destination $ffmpegDestination

foreach ($expected in @(
    'Music SSP FX.exe',
    'Resources\sidecar\demucs_repo\demucs\states.py',
    'Resources\sidecar\worker\worker_main.py',
    'Resources\sidecar\models\roformer-manifest.json',
    'Resources\sidecar\Runtime\htdemucs-worker\htdemucs-worker.exe',
    'Resources\sidecar\Runtime\ffmpeg\bin\ffmpeg.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $stageRoot $expected) -PathType Leaf)) {
        throw "Portable package is incomplete, missing: $expected"
    }
}

Get-ChildItem -LiteralPath $stageRoot -Recurse -Filter '*.th' -File | ForEach-Object {
    throw "Portable package must not contain model weights: $($_.FullName)"
}
$personalDataHits = Get-ChildItem -LiteralPath $stageRoot -Recurse -File |
    Where-Object { $_.Extension -in @('.json', '.md', '.txt', '.xml', '.yaml') } |
    Select-String -Pattern '(?i)C:[\\/]+Users[\\/]|/Users/' -List
if ($personalDataHits) {
    $details = ($personalDataHits | ForEach-Object Path) -join [Environment]::NewLine
    throw "Refusing to package machine-identifying text:`n$details"
}

# Pack largest-first into volumes so no single archive exceeds the cap. Paths
# are preserved in every volume, so extracting them all into one folder
# reproduces the tree no matter how the files were distributed.
$allFiles = @(Get-ChildItem -LiteralPath $stageRoot -Recurse -File | Sort-Object Length -Descending)
$buckets = New-Object System.Collections.ArrayList
$bucketBytes = New-Object System.Collections.ArrayList
foreach ($file in $allFiles) {
    $placed = $false
    for ($i = 0; $i -lt $buckets.Count; $i++) {
        if ($bucketBytes[$i] + $file.Length -le $MaxArchiveBytes) {
            [void]$buckets[$i].Add($file)
            $bucketBytes[$i] = $bucketBytes[$i] + $file.Length
            $placed = $true
            break
        }
    }
    if (-not $placed) {
        if ($file.Length -gt $MaxArchiveBytes) {
            throw "A single file exceeds the archive limit: $($file.FullName)"
        }
        $newBucket = New-Object System.Collections.ArrayList
        [void]$newBucket.Add($file)
        [void]$buckets.Add($newBucket)
        [void]$bucketBytes.Add([int64]$file.Length)
    }
}
$volumeCount = $buckets.Count

$readme = @(
    'Music SSP FX - 免安裝版 / portable build',
    '',
    ('版本 / version: {0} ({1} runtime)' -f $Version, $Flavor)
)
if ($volumeCount -gt 1) {
    $readme += @(
        '',
        ('!! 本包分成 {0} 個壓縮檔，請「全部」解壓縮到同一個資料夾後再執行。' -f $volumeCount),
        ('!! This package is split into {0} archives. Extract ALL of them into' -f $volumeCount),
        '   the same folder before running it.'
    )
}
$readme += @(
    '',
    '用法：直接執行 "Music SSP FX.exe"。不需要安裝 Python、CUDA toolkit 或任何',
    '其他元件；CUDA 版只需要 NVIDIA 顯示卡驅動。',
    '',
    'Usage: run "Music SSP FX.exe". No Python or CUDA toolkit installation is',
    'needed; the CUDA build only requires an NVIDIA display driver.',
    '',
    '模型權重不隨本包散布。第一次用到某個模型時會自動下載並驗證 SHA-256，',
    '存放在 %LOCALAPPDATA%\Music SSP FX\ 之下。',
    '',
    'Model weights are not distributed with this package. Each model is',
    'downloaded on first use and verified against a pinned SHA-256, into',
    '%LOCALAPPDATA%\Music SSP FX\.',
    '',
    '第三方授權見 Licenses\ 目錄。/ Third-party notices are in Licenses\.'
)
$readmePath = Join-Path $stageRoot 'README.txt'
Set-Content -LiteralPath $readmePath -Value $readme -Encoding utf8
# The README is written after bucketing, so place it in the first volume.
[void]$buckets[0].Add((Get-Item -LiteralPath $readmePath))

$listRoot = Join-Path $projectRoot 'build\portable-lists'
New-Item -ItemType Directory -Path $listRoot -Force | Out-Null
$produced = @()
for ($i = 0; $i -lt $volumeCount; $i++) {
    $name = if ($volumeCount -eq 1) {
        "$baseName.zip"
    } else {
        "$baseName.part{0}of{1}.zip" -f ($i + 1), $volumeCount
    }
    $archive = Join-Path $distRoot $name
    $listPath = Join-Path $listRoot "$baseName-$i.txt"
    @($buckets[$i] | ForEach-Object {
        $_.FullName.Substring($stageRoot.Length + 1).Replace('\', '/')
    }) | Set-Content -LiteralPath $listPath -Encoding ascii

    Write-Host ("Compressing volume {0} of {1}..." -f ($i + 1), $volumeCount)
    Push-Location $stageRoot
    try {
        & "$env:SystemRoot\System32\tar.exe" --format zip -c -f "$archive.partial" -T $listPath
        if ($LASTEXITCODE -ne 0) { throw "tar.exe failed with exit code $LASTEXITCODE" }
    } finally {
        Pop-Location
    }
    Move-Item -LiteralPath "$archive.partial" -Destination $archive
    $produced += $archive
}

foreach ($archive in $produced) {
    $size = (Get-Item -LiteralPath $archive).Length
    if ($size -gt 2GB) {
        throw "Archive exceeds the 2 GiB release-asset limit: $archive"
    }
    Write-Output "portable_archive=$archive"
    Write-Output ("bytes={0} ({1:N1} MiB)" -f $size, ($size / 1MB))
    Write-Output ("sha256=" + (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant())
}

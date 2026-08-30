param(
    [ValidateSet('cuda', 'cpu')]
    [string]$Flavor = 'cuda',
    [string]$Version = '0.0.2',
    [string]$Ffmpeg = 'C:\ffmpeg-master\bin\ffmpeg.exe'
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
$archive = Join-Path $distRoot "Music_SSP_FX_Portable_win64_$Flavor-$Version.zip"

foreach ($required in @(
    (Join-Path $payloadRoot 'Music SSP FX.exe'),
    (Join-Path $runtimeDist 'htdemucs-worker.exe'),
    $Ffmpeg)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required component not found: $required"
    }
}

foreach ($path in @($stageRoot, $archive, "$archive.partial")) {
    if (Test-Path -LiteralPath $path) {
        $resolved = (Resolve-Path -LiteralPath $path).Path
        if (-not $resolved.StartsWith($projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove a path outside the project: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
New-Item -ItemType Directory -Path $stageRoot, $distRoot -Force | Out-Null

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

$readme = @(
    'Music SSP FX - 免安裝版 / portable build',
    '',
    ('版本 / version: {0} ({1} runtime)' -f $Version, $Flavor),
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
Set-Content -LiteralPath (Join-Path $stageRoot 'README.txt') -Value $readme -Encoding utf8

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
    Select-String -Pattern '(?i)C:[\\/]+Users[\\/]+|/Users/' -List
if ($personalDataHits) {
    $details = ($personalDataHits | ForEach-Object Path) -join [Environment]::NewLine
    throw "Refusing to package machine-identifying text:`n$details"
}

Write-Host 'Compressing...'
Push-Location $stageRoot
try {
    & "$env:SystemRoot\System32\tar.exe" --format zip -c -f "$archive.partial" .
    if ($LASTEXITCODE -ne 0) { throw "tar.exe failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}
Move-Item -LiteralPath "$archive.partial" -Destination $archive

$size = (Get-Item -LiteralPath $archive).Length
Write-Output "portable_archive=$archive"
Write-Output ("bytes={0} ({1:N1} MiB)" -f $size, ($size / 1MB))
Write-Output ("sha256=" + (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant())

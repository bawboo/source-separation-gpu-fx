param(
    [string]$Version = '0.1.0'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$distRoot = Join-Path $projectRoot 'dist\windows-web'
$git = Get-Command git.exe -ErrorAction SilentlyContinue
if ($null -eq $git) {
    throw 'git.exe is required for the public repository audit.'
}

$safeProjectRoot = $projectRoot.Replace('\', '/')
$candidates = @(& $git.Source -c "safe.directory=$safeProjectRoot" -C $projectRoot `
    ls-files --cached --others --exclude-standard)
if ($LASTEXITCODE -ne 0) {
    throw 'The project must be initialized as a Git repository before auditing.'
}

$forbiddenExtensions = @('.th', '.nm', '.pt', '.pth', '.ckpt', '.safetensors')
$forbiddenDirectories = @('build/', 'dist/', 'Release/', 'results/')
$violations = @()
foreach ($relative in $candidates) {
    $normalized = $relative.Replace('\', '/')
    if ([IO.Path]::GetExtension($relative).ToLowerInvariant() -in $forbiddenExtensions) {
        $violations += "model weight: $relative"
    }
    if ($forbiddenDirectories | Where-Object { $normalized.StartsWith($_) }) {
        $violations += "generated directory: $relative"
    }
    $path = Join-Path $projectRoot $relative
    if ((Test-Path -LiteralPath $path -PathType Leaf) -and
        (Get-Item -LiteralPath $path).Length -gt 50MB) {
        $violations += "source candidate exceeds 50 MiB: $relative"
    }
}
if ($violations) {
    throw "Public repository audit failed:`n$($violations -join [Environment]::NewLine)"
}

$textExtensions = @('.cpp', '.h', '.py', '.ps1', '.cmd', '.iss', '.md', '.txt', '.json', '.yaml', '.yml', '.xml')
$identityPattern = '(?i)C:[\\/]+Users[\\/]+[^\\/]+(?:[\\/]|$)|/Users/[^/]+(?:/|$)|Documents[\\/]+Codex|AppData[\\/]+Local[\\/]+Temp'
$identityHits = @()
foreach ($relative in $candidates) {
    $path = Join-Path $projectRoot $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        continue
    }
    if ([IO.Path]::GetExtension($path).ToLowerInvariant() -notin $textExtensions) {
        continue
    }
    $match = Select-String -LiteralPath $path -Pattern $identityPattern -List
    if ($match) {
        $identityHits += $relative
    }
}
if ($identityHits) {
    throw "Machine-identifying text found:`n$($identityHits -join [Environment]::NewLine)"
}

$expectedAssets = @(
    "runtime-win-x64-cpu-$Version.zip",
    "runtime-win-x64-cuda-core-$Version.zip",
    "runtime-win-x64-cuda-libraries-$Version.zip",
    "runtime-win-x64-cpu-$Version.json",
    "runtime-win-x64-cuda-$Version.json",
    'release-manifest.json'
)
$assetPaths = @()
foreach ($name in $expectedAssets) {
    $path = Join-Path $distRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Expected release asset is missing: $path"
    }
    if ((Get-Item -LiteralPath $path).Length -ge 2GB) {
        throw "Release asset exceeds GitHub's 2 GiB per-file limit: $path"
    }
    $assetPaths += $path
}
$installerPath = Join-Path $distRoot 'installer\HTDemucs_GPU_FX_Setup_x64.exe'
if (Test-Path -LiteralPath $installerPath -PathType Leaf) {
    $assetPaths += $installerPath
}

$checksumPath = Join-Path $distRoot 'SHA256SUMS.txt'
$checksumLines = @($assetPaths | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($_))"
})
$checksumLines | Set-Content -LiteralPath $checksumPath -Encoding ascii

Write-Output 'public_repo_audit=PASS'
Write-Output "source_candidates=$($candidates.Count)"
Write-Output "checksums=$checksumPath"
foreach ($line in $checksumLines) {
    Write-Output $line
}

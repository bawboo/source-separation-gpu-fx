param(
    # No default. dist\windows-web keeps every version ever built, so a
    # default meant this could audit an old release's archives, find them
    # intact, and report PASS for a release it had not looked at.
    [Parameter(Mandatory = $true)]
    [string]$Version
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
# A regex that looks for user paths necessarily contains one, so the packaging
# scripts that run this same guard -- and this file -- matched themselves and
# the audit could never pass. Skip lines that are a character class rather than
# a path, and vendored third-party trees, which are upstream's to sanitise.
$patternDefinition = '\[\\{1,2}/\]'
$identityHits = @()
foreach ($relative in $candidates) {
    $path = Join-Path $projectRoot $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        continue
    }
    if ($relative.Replace([char]92, '/').StartsWith('third_party/')) {
        continue
    }
    if ([IO.Path]::GetExtension($path).ToLowerInvariant() -notin $textExtensions) {
        continue
    }
    $matches = @(Select-String -LiteralPath $path -Pattern $identityPattern |
        Where-Object { $_.Line -notmatch $patternDefinition })
    if ($matches.Count -gt 0) {
        $identityHits += "$relative : $($matches[0].Line.Trim())"
    }
}
if ($identityHits) {
    throw "Machine-identifying text found:`n$($identityHits -join [Environment]::NewLine)"
}

# The runtime archives come from the manifests rather than a hand-written list:
# the CUDA runtime is split into as many volumes as the 2 GiB asset limit needs,
# and a fixed list silently stopped covering the third one.
$archiveNames = @()
foreach ($flavor in @('cpu', 'cuda')) {
    $manifestName = "runtime-win-x64-$flavor-$Version.json"
    $manifestPath = Join-Path $distRoot $manifestName
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Expected release asset is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding utf8 | ConvertFrom-Json
    $archiveNames += @($manifest.archives | ForEach-Object { $_.archive })
}
$requiredNames = @($archiveNames) + @(
    "runtime-win-x64-cpu-$Version.json",
    "runtime-win-x64-cuda-$Version.json",
    'release-manifest.json')
foreach ($name in $requiredNames) {
    $path = Join-Path $distRoot $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Expected release asset is missing: $path"
    }
    if ((Get-Item -LiteralPath $path).Length -ge 2GB) {
        throw "Release asset exceeds GitHub's 2 GiB per-file limit: $path"
    }
}

# Checksum what is actually published: the runtime archives, the installer and
# the portable packages. The manifests are build inputs, not download targets.
$assetPaths = @($archiveNames | ForEach-Object { Join-Path $distRoot $_ })
$installerPath = Join-Path $distRoot 'installer\Music_SSP_FX_Setup_x64.exe'
if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
    throw "Expected release asset is missing: $installerPath"
}
$assetPaths += $installerPath
$portableRoot = Join-Path $projectRoot 'dist\portable'
if (Test-Path -LiteralPath $portableRoot -PathType Container) {
    $assetPaths += @(Get-ChildItem -LiteralPath $portableRoot -File `
        -Filter "Music_SSP_FX_Portable_win64_*-$Version*.zip" | ForEach-Object FullName)
}
foreach ($path in $assetPaths) {
    if ((Get-Item -LiteralPath $path).Length -ge 2GB) {
        throw "Release asset exceeds GitHub's 2 GiB per-file limit: $path"
    }
}

$checksumPath = Join-Path $distRoot 'SHA256SUMS.txt'
$checksumLines = @($assetPaths | ForEach-Object {
    $hash = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $([IO.Path]::GetFileName($_))"
} | Sort-Object)
$checksumLines | Set-Content -LiteralPath $checksumPath -Encoding ascii

Write-Output 'public_repo_audit=PASS'
Write-Output "source_candidates=$($candidates.Count)"
Write-Output "checksums=$checksumPath"
foreach ($line in $checksumLines) {
    Write-Output $line
}

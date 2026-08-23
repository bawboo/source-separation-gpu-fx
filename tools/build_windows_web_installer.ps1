param(
    [Parameter(Mandatory = $true)]
    [string]$ReleaseBaseUrl,
    [string]$Version = '0.1.0',
    [string]$Iscc = '',
    [string]$OutputDirectory = '',
    [switch]$AllowNonGitHubUrlForCompileTest
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$distRoot = Join-Path $projectRoot 'dist\windows-web'
$payloadRoot = Join-Path $projectRoot 'build\windows-web\payload'
$iss = Join-Path $projectRoot 'packaging\windows\HTDemucsGpuFX-Web.iss'

if ([string]::IsNullOrWhiteSpace($Iscc)) {
    $command = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $Iscc = $command.Source
    } else {
        $candidates = @(
            "$env:ProgramFiles\Inno Setup 7\ISCC.exe",
            "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
            "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
        )
        $Iscc = $candidates | Where-Object {
            Test-Path -LiteralPath $_ -PathType Leaf
        } | Select-Object -First 1
    }
}
if (-not (Test-Path -LiteralPath $Iscc -PathType Leaf)) {
    throw 'Inno Setup ISCC.exe was not found. Install Inno Setup or pass -Iscc.'
}
if (-not (Test-Path -LiteralPath $payloadRoot -PathType Container)) {
    throw "Installer payload is missing: $payloadRoot"
}

$runtime = @{}
foreach ($flavor in @('cpu', 'cuda')) {
    $manifestPath = Join-Path $distRoot "runtime-win-x64-$flavor-$Version.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Runtime manifest is missing: $manifestPath"
    }
    $runtime[$flavor] = Get-Content -LiteralPath $manifestPath -Raw -Encoding utf8 |
        ConvertFrom-Json
}

$baseUrl = $ReleaseBaseUrl.TrimEnd('/')
$releaseUri = $null
if (-not [Uri]::TryCreate($baseUrl, [UriKind]::Absolute, [ref]$releaseUri)) {
    throw "ReleaseBaseUrl is not an absolute URL: $baseUrl"
}
$expectedPathSuffix = "/releases/download/v$Version"
if (-not $AllowNonGitHubUrlForCompileTest -and
    ($releaseUri.Scheme -ne 'https' -or
     $releaseUri.Host -ne 'github.com' -or
     -not $releaseUri.AbsolutePath.EndsWith(
        $expectedPathSuffix, [StringComparison]::OrdinalIgnoreCase))) {
    throw "ReleaseBaseUrl must be an exact GitHub tag URL ending in $expectedPathSuffix"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $distRoot 'installer'
} elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$cpuArchives = @($runtime.cpu.archives)
$cudaArchives = @($runtime.cuda.archives)
if ($cpuArchives.Count -ne 1) {
    throw 'The CPU runtime manifest must contain exactly one archive.'
}
if ($cudaArchives.Count -ne 2) {
    throw 'The CUDA runtime manifest must contain exactly two archives.'
}
$cudaCore = @($cudaArchives | Where-Object role -eq 'core')
$cudaLibraries = @($cudaArchives | Where-Object role -eq 'cuda-libraries')
if ($cudaCore.Count -ne 1 -or $cudaLibraries.Count -ne 1) {
    throw 'The CUDA runtime manifest must contain core and cuda-libraries roles.'
}
$cpuRuntime = $cpuArchives[0]
$cudaCoreRuntime = $cudaCore[0]
$cudaLibrariesRuntime = $cudaLibraries[0]
$defines = @(
    "/DAppVersion=$Version",
    "/DPayloadRoot=$payloadRoot",
    "/DOutputDirectory=$OutputDirectory",
    "/DCpuRuntimeUrl=$baseUrl/$($cpuRuntime.archive)",
    "/DCpuRuntimeBytes=$($cpuRuntime.bytes)",
    "/DCpuRuntimeSha256=$($cpuRuntime.sha256)",
    "/DCudaCoreRuntimeUrl=$baseUrl/$($cudaCoreRuntime.archive)",
    "/DCudaCoreRuntimeBytes=$($cudaCoreRuntime.bytes)",
    "/DCudaCoreRuntimeSha256=$($cudaCoreRuntime.sha256)",
    "/DCudaLibrariesRuntimeUrl=$baseUrl/$($cudaLibrariesRuntime.archive)",
    "/DCudaLibrariesRuntimeBytes=$($cudaLibrariesRuntime.bytes)",
    "/DCudaLibrariesRuntimeSha256=$($cudaLibrariesRuntime.sha256)"
)
& $Iscc @defines $iss
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup compilation failed with exit code $LASTEXITCODE"
}

$installer = Join-Path $OutputDirectory 'HTDemucs_GPU_FX_Setup_x64.exe'
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Installer was not produced: $installer"
}
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "installer=$installer"
Write-Output "bytes=$((Get-Item -LiteralPath $installer).Length)"
Write-Output "sha256=$hash"

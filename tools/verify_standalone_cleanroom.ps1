param(
    [string]$PortableRoot = '',
    [string]$SmokeTest = ''
)

$ErrorActionPreference = 'Stop'
# The desktop host exports both Path and PATH. .NET's ProcessStartInfo treats
# environment keys case-insensitively, so normalize this verifier process first.
$processPath = [Environment]::GetEnvironmentVariable('Path', 'Process')
[Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
[Environment]::SetEnvironmentVariable('Path', $processPath, 'Process')
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($PortableRoot)) {
    $PortableRoot = Join-Path $projectRoot 'dist\Music SSP FX Portable'
}
if ([string]::IsNullOrWhiteSpace($SmokeTest)) {
    $SmokeTest = Join-Path $projectRoot 'build\plugin\Release\htdemucs_standalone_portable_gpu_smoke.exe'
}
$PortableRoot = (Resolve-Path -LiteralPath $PortableRoot).Path
$SmokeTest = (Resolve-Path -LiteralPath $SmokeTest).Path
$portableExecutable = Join-Path $PortableRoot 'Music SSP FX.exe'
$workerExecutable = Join-Path $PortableRoot 'Resources\sidecar\Runtime\htdemucs-worker\htdemucs-worker.exe'
$ffmpegExecutable = Join-Path $PortableRoot 'Resources\sidecar\Runtime\ffmpeg\bin\ffmpeg.exe'
$smokeDestination = Join-Path $PortableRoot 'htdemucs_standalone_portable_gpu_smoke.exe'

foreach ($required in @($portableExecutable, $workerExecutable, $ffmpegExecutable, $SmokeTest)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Clean-room validation component missing: $required"
    }
}

$profileRoot = Join-Path $projectRoot 'build\cleanroom-profile'
$resolvedBuild = (Resolve-Path -LiteralPath (Join-Path $projectRoot 'build')).Path
if ((Split-Path -Parent $profileRoot) -ne $resolvedBuild) {
    throw 'Refusing to replace a clean-room folder outside the project build directory.'
}
if (Test-Path -LiteralPath $profileRoot) {
    Remove-Item -LiteralPath $profileRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $profileRoot -Force | Out-Null

function New-CleanProcess {
    param([string]$FileName, [string]$Arguments = '')
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FileName
    $info.Arguments = $Arguments
    $info.WorkingDirectory = $PortableRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.EnvironmentVariables.Clear()
    $info.EnvironmentVariables['SystemRoot'] = $env:SystemRoot
    $info.EnvironmentVariables['WINDIR'] = $env:WINDIR
    $info.EnvironmentVariables['TEMP'] = $profileRoot
    $info.EnvironmentVariables['TMP'] = $profileRoot
    $info.EnvironmentVariables['USERPROFILE'] = $profileRoot
    $info.EnvironmentVariables['APPDATA'] = (Join-Path $profileRoot 'AppData\Roaming')
    $info.EnvironmentVariables['LOCALAPPDATA'] = (Join-Path $profileRoot 'AppData\Local')
    $info.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $info.EnvironmentVariables['HTFX_REQUIRE_BUNDLED_SIDECAR'] = '1'
    return $info
}

function Invoke-CleanProcess {
    param([string]$FileName, [string]$Arguments = '', [int]$TimeoutMilliseconds = 180000)
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = New-CleanProcess -FileName $FileName -Arguments $Arguments
    if (-not $process.Start()) {
        throw "Could not start clean-room process: $FileName"
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutMilliseconds)) {
        $process.Kill()
        throw "Clean-room process timed out: $FileName"
    }
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    if ($process.ExitCode -ne 0) {
        throw "Clean-room process failed ($($process.ExitCode)): $FileName`n$stdout`n$stderr"
    }
    return [ordered]@{ ExitCode = $process.ExitCode; Stdout = $stdout; Stderr = $stderr }
}

Copy-Item -LiteralPath $SmokeTest -Destination $smokeDestination -Force
try {
    $workerHelp = Invoke-CleanProcess -FileName $workerExecutable -Arguments '--help' -TimeoutMilliseconds 30000
    if ($workerHelp.Stdout -notmatch '--models-dir') {
        throw 'Bundled worker help output is incomplete.'
    }
    $ffmpegVersion = Invoke-CleanProcess -FileName $ffmpegExecutable -Arguments '-version' -TimeoutMilliseconds 30000
    if ($ffmpegVersion.Stdout -notmatch 'ffmpeg version') {
        throw 'Bundled FFmpeg version check is incomplete.'
    }
    $smoke = Invoke-CleanProcess -FileName $smokeDestination -TimeoutMilliseconds 180000
    if ($smoke.Stdout -notmatch 'PASS=true') {
        throw "Bundled CUDA inference did not pass:`n$($smoke.Stdout)`n$($smoke.Stderr)"
    }

    $gui = [System.Diagnostics.Process]::new()
    $gui.StartInfo = New-CleanProcess -FileName $portableExecutable
    if (-not $gui.Start()) {
        throw 'Could not launch the portable Standalone GUI.'
    }
    Start-Sleep -Seconds 8
    if ($gui.HasExited) {
        throw "Portable Standalone GUI exited early with code $($gui.ExitCode)."
    }
    $gui.Kill()
    $gui.WaitForExit()

    Write-Output 'cleanroom_external_python=false'
    Write-Output 'cleanroom_external_ffmpeg=false'
    Write-Output 'worker_help=PASS'
    Write-Output 'ffmpeg=PASS'
    Write-Output 'cuda_inference=PASS'
    Write-Output 'standalone_gui_launch=PASS'
    Write-Output ($smoke.Stdout.Trim())
} finally {
    if (Test-Path -LiteralPath $smokeDestination -PathType Leaf) {
        Remove-Item -LiteralPath $smokeDestination -Force
    }
}

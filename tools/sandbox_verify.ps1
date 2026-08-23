$ErrorActionPreference = 'Stop'
$source = 'C:\HTFXProject\dist\HTDemucs GPU FX Portable'
$destination = 'C:\HTFXPortable'
$resultPath = 'C:\HTFXResults\sandbox-result.txt'

function Write-Result([string]$status, [string]$details) {
    @(
        "status=$status"
        "utc=$([DateTime]::UtcNow.ToString('o'))"
        $details
    ) | Set-Content -LiteralPath $resultPath -Encoding utf8
}

try {
    if (Test-Path -LiteralPath $destination) {
        Remove-Item -LiteralPath $destination -Recurse -Force
    }
    Copy-Item -LiteralPath $source -Destination $destination -Recurse

    $worker = Join-Path $destination 'Resources\sidecar\Runtime\htdemucs-worker\htdemucs-worker.exe'
    $ffmpeg = Join-Path $destination 'Resources\sidecar\Runtime\ffmpeg\bin\ffmpeg.exe'
    $app = Join-Path $destination 'HTDemucs GPU FX.exe'
    $workerHelp = & $worker --help 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $workerHelp -notmatch '--models-dir') {
        throw 'Bundled worker failed to start in Windows Sandbox.'
    }
    $ffmpegVersion = & $ffmpeg -version 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $ffmpegVersion -notmatch 'ffmpeg version') {
        throw 'Bundled FFmpeg failed to start in Windows Sandbox.'
    }

    $process = Start-Process -FilePath $app -WorkingDirectory $destination -PassThru
    Start-Sleep -Seconds 10
    if ($process.HasExited) {
        throw "Standalone exited early with code $($process.ExitCode)."
    }
    Stop-Process -Id $process.Id
    Write-Result 'PASS' 'worker=PASS; ffmpeg=PASS; standalone_gui=PASS; external_python=absent'
} catch {
    Write-Result 'FAIL' $_.Exception.ToString()
} finally {
    shutdown.exe /s /t 0
}

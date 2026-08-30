param(
    [string]$Vst3Directory = "$env:CommonProgramFiles\VST3",
    [switch]$KeepPythonSetting
)

$ErrorActionPreference = 'Stop'
$destination = Join-Path $Vst3Directory 'Music SSP FX.vst3'
if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
    Write-Output "removed=$destination"
}
if (-not $KeepPythonSetting) {
    [Environment]::SetEnvironmentVariable('HTFX_PYTHON', $null, 'User')
    Write-Output 'removed_user_environment=HTFX_PYTHON'
}

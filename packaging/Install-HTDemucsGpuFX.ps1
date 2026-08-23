param(
    [Parameter(Mandatory = $true)]
    [string]$PackageBundle,
    [string]$Python = '',
    [string]$Vst3Directory = "$env:CommonProgramFiles\VST3"
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Python)) {
    $commonCandidates = @(
        (Join-Path $env:USERPROFILE 'anaconda3\python.exe'),
        (Join-Path $env:USERPROFILE 'miniconda3\python.exe')
    )
    $Python = $commonCandidates | Where-Object {
        Test-Path -LiteralPath $_ -PathType Leaf
    } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($Python)) {
        $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
        if ($null -ne $pythonCommand) {
            $Python = $pythonCommand.Source
        }
    }
}
$bundle = (Resolve-Path -LiteralPath $PackageBundle).Path
if (-not (Test-Path -LiteralPath $bundle -PathType Container) -or -not $bundle.EndsWith('.vst3')) {
    throw "Invalid VST3 bundle: $PackageBundle"
}
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    throw "CUDA Python not found: $Python"
}
& $Python -c "import torch; assert torch.cuda.is_available(); print(torch.__version__, torch.cuda.get_device_name(0))"
if ($LASTEXITCODE -ne 0) {
    throw 'The selected Python cannot use CUDA.'
}

New-Item -ItemType Directory -Path $Vst3Directory -Force | Out-Null
$destination = Join-Path $Vst3Directory 'HTDemucs GPU FX.vst3'
if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
}
Copy-Item -LiteralPath $bundle -Destination $destination -Recurse
[Environment]::SetEnvironmentVariable('HTFX_PYTHON', (Resolve-Path -LiteralPath $Python).Path, 'User')
Write-Output "installed=$destination"
Write-Output "HTFX_PYTHON=$((Resolve-Path -LiteralPath $Python).Path)"

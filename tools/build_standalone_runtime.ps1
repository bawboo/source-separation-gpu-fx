param(
    [string]$Python = '',
    [ValidateSet('cuda', 'cpu')]
    [string]$Flavor = 'cuda'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildRoot = Join-Path $projectRoot 'build'
$toolRoot = Join-Path $buildRoot 'packaging-tools'
$suffix = if ($Flavor -eq 'cuda') { '' } else { '-cpu' }
$distRoot = Join-Path $buildRoot "standalone-runtime${suffix}-dist"
$workRoot = Join-Path $buildRoot "standalone-runtime${suffix}-work"
$specRoot = Join-Path $buildRoot "standalone-runtime${suffix}-spec"
$workerSource = Join-Path $projectRoot 'worker\worker_main.py'
$hookRoot = Join-Path $projectRoot 'tools\pyinstaller-hooks'
$demucsRoot = Join-Path $projectRoot 'third_party\demucs'

if (-not (Test-Path -LiteralPath (Join-Path $demucsRoot 'demucs\__init__.py') -PathType Leaf)) {
    throw 'Demucs submodule is missing. Run: git submodule update --init --recursive'
}

if ([string]::IsNullOrWhiteSpace($Python)) {
    $pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -eq $pythonCommand) {
        throw 'Python was not supplied and python.exe was not found on PATH.'
    }
    $Python = $pythonCommand.Source
}
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    throw "Python not found: $Python"
}
# PyInstaller is installed with `pip --target`, so its tree is tied to the
# interpreter version that installed it. The CPU and CUDA runtimes are frozen by
# different interpreters, so keep one tool directory per version.
$pythonTag = (& $Python -c "import sys; print(f'py{sys.version_info.major}{sys.version_info.minor}')")
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($pythonTag)) {
    throw "Unable to query the Python version of $Python"
}
$toolRoot = "$toolRoot-$pythonTag"
if (-not (Test-Path -LiteralPath (Join-Path $toolRoot 'PyInstaller\__main__.py') -PathType Leaf)) {
    Write-Host "Installing PyInstaller for $pythonTag into $toolRoot ..."
    & $Python -m pip install --disable-pip-version-check --no-warn-script-location `
        --target $toolRoot pyinstaller
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to install PyInstaller into $toolRoot"
    }
}

$runtimeCheck = if ($Flavor -eq 'cuda') {
    "import torch; assert torch.version.cuda is not None; assert torch.cuda.is_available(); print(torch.__version__, torch.version.cuda, torch.cuda.get_device_name(0))"
} else {
    "import torch; assert torch.version.cuda is None; print(torch.__version__, 'cpu-only')"
}
& $Python -c $runtimeCheck
if ($LASTEXITCODE -ne 0) {
    throw "The selected Python is not a usable $Flavor PyTorch environment."
}

foreach ($target in @($distRoot, $workRoot, $specRoot)) {
    if (Test-Path -LiteralPath $target) {
        $resolvedParent = (Resolve-Path -LiteralPath (Split-Path -Parent $target)).Path
        if (-not $resolvedParent.Equals($buildRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to replace a runtime build folder outside $buildRoot"
        }
        Remove-Item -LiteralPath $target -Recurse -Force
    }
}
New-Item -ItemType Directory -Path $distRoot,$workRoot,$specRoot -Force | Out-Null

$oldPythonPath = $env:PYTHONPATH
$env:PYTHONPATH = (@(
    $toolRoot,
    (Join-Path $projectRoot 'worker'),
    (Join-Path $projectRoot 'src'),
    $demucsRoot,
    $oldPythonPath
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) -join ';'
$buildPythonPath = $env:PYTHONPATH

try {
    $arguments = @(
        '-m', 'PyInstaller',
        '--noconfirm',
        '--clean',
        '--onedir',
        '--console',
        '--name', 'htdemucs-worker',
        '--distpath', $distRoot,
        '--workpath', $workRoot,
        '--specpath', $specRoot,
        '--additional-hooks-dir', $hookRoot,
        '--paths', (Join-Path $projectRoot 'worker'),
        '--paths', (Join-Path $projectRoot 'src'),
        '--paths', $demucsRoot,
        '--hidden-import', 'yaml',
        '--hidden-import', 'demucs.pretrained',
        '--hidden-import', 'demucs.states',
        '--hidden-import', 'demucs.apply',
        '--hidden-import', 'demucs.htdemucs',
        '--hidden-import', 'gpu_ipc_worker',
        # Checkpoints pickled against NumPy 1.x name numpy.core.*, which NumPy 2
        # keeps only as a compatibility shim that static analysis never sees.
        # Without it, torch.load fails with ModuleNotFoundError at unpickle time.
        '--collect-submodules', 'numpy.core',
        # MelBand RoFormer shares this bundle so torch is packaged once, not twice.
        '--hidden-import', 'roformer_worker',
        '--hidden-import', 'roformer_cache',
        '--hidden-import', 'soundfile',
        '--hidden-import', 'librosa',
        '--hidden-import', 'ml_collections',
        '--hidden-import', 'beartype',
        '--hidden-import', 'packaging',
        '--collect-submodules', 'mel_band_roformer',
        '--collect-submodules', 'rotary_embedding_torch',
        '--collect-data', 'mel_band_roformer',
        '--collect-submodules', 'einops',
        '--collect-submodules', 'julius',
        '--exclude-module', 'mlx',
        '--exclude-module', 'mlx_spectro',
        '--exclude-module', 'tensorflow',
        '--exclude-module', 'keras',
        '--exclude-module', 'matplotlib',
        '--exclude-module', 'pandas',
        '--exclude-module', 'sklearn',
        '--exclude-module', 'pytest',
        '--exclude-module', 'setuptools',
        '--exclude-module', 'sphinx',
        '--exclude-module', 'docutils',
        '--exclude-module', 'dask',
        '--exclude-module', 'distributed',
        '--exclude-module', 'pyarrow',
        '--exclude-module', 'lxml',
        '--exclude-module', 'PIL',
        '--exclude-module', 'cv2',
        '--exclude-module', 'zmq',
        '--exclude-module', 'tkinter',
        '--exclude-module', 'PyQt5',
        '--exclude-module', 'cryptography',
        '--exclude-module', 'bcrypt',
        '--exclude-module', 'nacl',
        '--exclude-module', 'transformers',
        '--exclude-module', 'datasets',
        '--exclude-module', 'spacy',
        '--exclude-module', 'thinc',
        '--exclude-module', 'pydantic',
        '--exclude-module', 'rich',
        '--exclude-module', 'botocore',
        '--exclude-module', 'boto3',
        '--exclude-module', 'torchvision',
        '--exclude-module', 'torchaudio',
        '--exclude-module', 'av',
        '--exclude-module', 'xformers',
        '--exclude-module', 'plotly',
        '--exclude-module', 'skimage',
        '--exclude-module', 'statsmodels',
        '--exclude-module', 'xarray',
        '--exclude-module', 'kaleido',
        '--exclude-module', 'patsy',
        '--exclude-module', 'paramiko',
        '--exclude-module', 'selenium',
        '--exclude-module', 'bokeh',
        '--exclude-module', 'sqlalchemy',
        '--exclude-module', 'h5py',
        '--exclude-module', 'cloudpickle',
        '--exclude-module', 'fsspec',
        '--exclude-module', 'lz4',
        '--exclude-module', 'torch.utils.tensorboard',
        '--exclude-module', 'torch.onnx',
        '--exclude-module', 'torch._dynamo',
        '--exclude-module', 'torch._inductor',
        '--exclude-module', 'pkg_resources',
        '--exclude-module', 'traitlets',
        '--exclude-module', 'pygments',
        '--exclude-module', 'py',
        '--exclude-module', 'IPython',
        '--exclude-module', 'notebook',
        '--exclude-module', 'jupyter',
        $workerSource
    )
    & $Python @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "PyInstaller failed with exit code $LASTEXITCODE"
    }
} finally {
    $env:PYTHONPATH = $oldPythonPath
}

$runtimeRoot = Join-Path $distRoot 'htdemucs-worker'
$workerExecutable = Join-Path $runtimeRoot 'htdemucs-worker.exe'
if (-not (Test-Path -LiteralPath $workerExecutable -PathType Leaf)) {
    throw "Self-contained worker was not created: $workerExecutable"
}

# These optional acceleration/developer trees are pulled in by binary probing
# on some broad Anaconda installations. HTDemucs does not import them; keeping
# them would add hundreds of megabytes and torch/bin contains no runtime DLLs.
foreach ($relative in @('_internal\xformers', '_internal\torch\bin')) {
    $candidate = Join-Path $runtimeRoot $relative
    if (Test-Path -LiteralPath $candidate) {
        $resolvedCandidate = (Resolve-Path -LiteralPath $candidate).Path
        if (-not $resolvedCandidate.StartsWith($runtimeRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to prune a path outside the runtime: $resolvedCandidate"
        }
        Remove-Item -LiteralPath $resolvedCandidate -Recurse -Force
    }
}

$helpOutput = & $workerExecutable --help 2>&1
if ($LASTEXITCODE -ne 0 -or ($helpOutput -join "`n") -notmatch '--models-dir') {
    throw "Self-contained worker import/startup check failed:`n$($helpOutput -join "`n")"
}
$roformerHelp = & $workerExecutable roformer --help 2>&1
if ($LASTEXITCODE -ne 0 -or ($roformerHelp -join "`n") -notmatch '--models-dir') {
    throw "RoFormer dispatch check failed:`n$($roformerHelp -join "`n")"
}

$runtimeBytes = (Get-ChildItem -LiteralPath $runtimeRoot -Recurse -File |
    Measure-Object Length -Sum).Sum
$env:PYTHONPATH = $buildPythonPath
try {
    $versions = & $Python -c "import json,sys,torch,numpy,demucs,einops,mel_band_roformer; print(json.dumps({'python':sys.version.split()[0],'torch':torch.__version__,'cuda':torch.version.cuda,'numpy':numpy.__version__,'demucs':getattr(demucs,'__version__','bundled'),'einops':einops.__version__,'mel_band_roformer':getattr(mel_band_roformer,'__version__','unknown')}))"
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to query the $Flavor runtime versions."
    }
} finally {
    $env:PYTHONPATH = $oldPythonPath
}
$runtimeManifest = [ordered]@{
    format = 'PyInstaller onedir'
    flavor = $Flavor
    entrypoint = 'htdemucs-worker.exe'
    backends = @('htdemucs', 'roformer')
    entrypoint_sha256 = (Get-FileHash -LiteralPath $workerExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
    runtime_bytes = $runtimeBytes
    versions = $versions | ConvertFrom-Json
    external_python_required = $false
    cuda_toolkit_required = $false
    nvidia_driver_required_for_cuda = ($Flavor -eq 'cuda')
}
$runtimeManifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $runtimeRoot 'runtime-manifest.json') -Encoding utf8

Write-Output "runtime=$runtimeRoot"
Write-Output "bytes=$runtimeBytes"
Write-Output "worker_sha256=$($runtimeManifest.entrypoint_sha256)"

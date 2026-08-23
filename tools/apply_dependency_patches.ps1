$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$juceRoot = Join-Path $projectRoot 'third_party\JUCE'
$patchPath = Join-Path $projectRoot 'patches\juce-8.0.13-htfx.patch'
$safeJuceRoot = $juceRoot.Replace('\', '/')

foreach ($required in @(
    (Join-Path $juceRoot 'CMakeLists.txt'),
    $patchPath
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required dependency file is missing: $required"
    }
}

$filterWindowPath = Join-Path $juceRoot `
    'modules\juce_audio_plugin_client\Standalone\juce_StandaloneFilterWindow.h'
$standaloneClientPath = Join-Path $juceRoot `
    'modules\juce_audio_plugin_client\juce_audio_plugin_client_Standalone.cpp'
$hasMmePatch = [IO.File]::ReadAllText($filterWindowPath).Contains(
    'createHTFXMMEAudioIODeviceType()')
$hasPortablePatch = [IO.File]::ReadAllText($standaloneClientPath).Contains(
    'HTFX_PORTABLE_STANDALONE_SETTINGS')
if ($hasMmePatch -and $hasPortablePatch) {
    Write-Output 'juce_patch=already_applied'
    exit 0
}
if ($hasMmePatch -or $hasPortablePatch) {
    throw 'JUCE contains only part of the HTFX patch; refusing an unsafe automatic merge.'
}

$ErrorActionPreference = 'Continue'
& git -c "safe.directory=$safeJuceRoot" -C $juceRoot apply --check $patchPath 2>$null
$forwardCheck = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($forwardCheck -eq 0) {
    & git -c "safe.directory=$safeJuceRoot" -C $juceRoot apply $patchPath
    if ($LASTEXITCODE -ne 0) {
        throw 'Applying the HTFX JUCE patch failed.'
    }
    Write-Output 'juce_patch=applied'
    exit 0
}

throw 'JUCE has unexpected local changes or the pinned version changed; the HTFX patch cannot be applied safely.'

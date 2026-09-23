param(
    [string]$JuceDir = $env:JUCE_DIR,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build'
# Some launchers provide both Path and PATH; MSBuild requires one entry.
$buildSearchPath = $env:Path
Remove-Item Env:Path -ErrorAction SilentlyContinue
$env:PATH = $buildSearchPath
$configure = @('-S', $projectRoot, '-B', $buildDirectory, '-G', 'Visual Studio 17 2022', '-A', 'x64')
if (-not [string]::IsNullOrWhiteSpace($JuceDir)) { $configure += "-DJUCE_DIR=$JuceDir" }
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $buildDirectory --config $Configuration --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
& ctest --test-dir $buildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Validation failed.' }
Write-Output (Join-Path $buildDirectory "AutoShapeStereoVST3_artefacts\$Configuration\VST3\Auto Shape Stereo.vst3")

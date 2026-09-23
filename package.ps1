$ErrorActionPreference = 'Stop'
$projectDirectory = $PSScriptRoot
$bundleName = 'Auto Shape Stereo.vst3'
$builtBundle = Join-Path $projectDirectory "build\AutoShapeStereoVST3_artefacts\Release\VST3\$bundleName"
$distribution = Join-Path $projectDirectory 'dist'
if (-not (Test-Path -LiteralPath (Join-Path $builtBundle "Contents\x86_64-win\$bundleName"))) {
    throw 'Build the Release VST3 before packaging.'
}
New-Item -ItemType Directory -Path $distribution -Force | Out-Null
$distBundle = Join-Path $distribution $bundleName
New-Item -ItemType Directory -Path $distBundle -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $builtBundle 'Contents') -Destination $distBundle -Recurse -Force
foreach ($filename in @('README.md', 'VALIDATION.md', 'THIRD_PARTY_NOTICES.md', 'Install-Auto-Shape-Stereo.ps1')) {
    Copy-Item -LiteralPath (Join-Path $projectDirectory $filename) -Destination $distribution -Force
}
Copy-Item -LiteralPath (Join-Path $projectDirectory 'Licenses') -Destination $distribution -Recurse -Force
$distBinary = Join-Path $distBundle "Contents\x86_64-win\$bundleName"
$binaryHash = (Get-FileHash -LiteralPath $distBinary -Algorithm SHA256).Hash
Set-Content -LiteralPath (Join-Path $distribution 'SHA256.txt') -Value "$binaryHash  Auto Shape Stereo.vst3/Contents/x86_64-win/Auto Shape Stereo.vst3" -Encoding utf8
$zipPath = Join-Path $projectDirectory 'Auto-Shape-Stereo-0.1.0-Windows.zip'
Compress-Archive -LiteralPath $distBundle,(Join-Path $distribution 'README.md'),(Join-Path $distribution 'VALIDATION.md'),(Join-Path $distribution 'THIRD_PARTY_NOTICES.md'),(Join-Path $distribution 'Licenses'),(Join-Path $distribution 'Install-Auto-Shape-Stereo.ps1'),(Join-Path $distribution 'SHA256.txt') -DestinationPath $zipPath -Force
Write-Output $zipPath

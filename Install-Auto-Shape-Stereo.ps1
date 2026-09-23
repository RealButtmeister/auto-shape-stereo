param([string]$Destination = (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'))
$ErrorActionPreference = 'Stop'
$bundleName = 'Auto Shape Stereo.vst3'
$bundleSource = Join-Path $PSScriptRoot $bundleName
if (-not (Test-Path -LiteralPath $bundleSource -PathType Container)) {
    $bundleSource = Join-Path $PSScriptRoot "dist\$bundleName"
}
if (-not (Test-Path -LiteralPath (Join-Path $bundleSource "Contents\x86_64-win\$bundleName") -PathType Leaf)) {
    throw 'The compiled plugin bundle was not found beside this installer or in dist.'
}
$installDirectory = [System.IO.Path]::GetFullPath($Destination)
$installBundle = Join-Path $installDirectory $bundleName
if (Test-Path -LiteralPath $installBundle) {
    throw "A plugin already exists at $installBundle. Keep a backup before replacing an older build."
}
New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
Copy-Item -LiteralPath $bundleSource -Destination $installBundle -Recurse
$sourceBinary = Join-Path $bundleSource "Contents\x86_64-win\$bundleName"
$installedBinary = Join-Path $installBundle "Contents\x86_64-win\$bundleName"
if ((Get-FileHash -LiteralPath $sourceBinary).Hash -ne (Get-FileHash -LiteralPath $installedBinary).Hash) {
    throw 'The installed binary did not match the source. Installation needs inspection.'
}
Write-Output "Installed: $installBundle"
Write-Output 'Rescan plugins in your DAW, then insert Auto Shape Stereo.'
Write-Output 'Play audio, click Auto Shape to learn, and click again to hold the settings.'

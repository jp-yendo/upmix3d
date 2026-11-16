param([string]$Destination = (Join-Path $PSScriptRoot '..\external'))
$ErrorActionPreference = 'Stop'
$version = '3.0.24'
$expectedHash = '1ed59c09152e78aff84663fa76efab26057716576db74332858810e5ef54ae1f'
$destinationPath = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null
$archivePath = Join-Path $destinationPath "vlc-$version-win64.7z"
$archiveUrl = "https://download.videolan.org/pub/videolan/vlc/$version/win64/vlc-$version-win64.7z"
if (!(Test-Path -LiteralPath $archivePath)) {
    & curl.exe -L --fail --max-time 180 $archiveUrl -o $archivePath
    if ($LASTEXITCODE -ne 0) { throw 'VLC download failed' }
}
$actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
if ($actualHash -ne $expectedHash) { throw 'VLC SHA256 mismatch; archive was not extracted' }
& tar.exe -xf $archivePath -C $destinationPath
if ($LASTEXITCODE -ne 0) { throw 'VLC extraction failed' }
Write-Output "VLC SDK ready: $(Join-Path $destinationPath "vlc-$version\sdk")"

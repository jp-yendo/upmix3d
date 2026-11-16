param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$VlcRoot = (Join-Path $PSScriptRoot '..\external\vlc-3.0.24')
)
$ErrorActionPreference = 'Stop'
$projectPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakePath = $cmakeCommand.Source }
else {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsInstall = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath = Join-Path $vsInstall 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (!(Test-Path -LiteralPath $cmakePath)) { throw 'CMake not found; install the Visual Studio C++ CMake tools component' }
$buildPath = Join-Path $projectPath 'build'
& $cmakePath -S $projectPath -B $buildPath -G 'Visual Studio 18 2026' -A x64 "-DVLC_ROOT=$([IO.Path]::GetFullPath($VlcRoot))"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& $cmakePath --build $buildPath --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
$ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
& $ctestPath --test-dir $buildPath -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }

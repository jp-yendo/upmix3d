param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$DeviceTests,
    [switch]$LongDsp
)
$ErrorActionPreference = 'Stop'
$projectPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location -LiteralPath $projectPath
try {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
    $executable = Join-Path $projectPath "build\$Configuration\upmix3d_integration.exe"
    function Invoke-IntegrationCase([string[]]$CaseArguments) {
        & $executable --plugin "build/$Configuration/libupmix3d_plugin.dll" @CaseArguments
        if ($LASTEXITCODE -ne 0) { throw "Native integration failed: $($CaseArguments -join ' ')" }
    }
    if ($DeviceTests) {
        Invoke-IntegrationCase -CaseArguments @('--case','spatial','--expect','route spatial')
        Invoke-IntegrationCase -CaseArguments @('--case','shared','--route','shared','--layout','7.1','--expect','route shared')
        Invoke-IntegrationCase -CaseArguments @('--case','exclusive','--route','exclusive','--layout','7.1','--method','matrix','--expect','route exclusive')
        Invoke-IntegrationCase -CaseArguments @('--case','overhead','--layout','5.1.2','--top-middle','--expect','route spatial')
        Invoke-IntegrationCase -CaseArguments @('--case','back-center','--channels','9','--expect','Input layout 8.1')
        Invoke-IntegrationCase -CaseArguments @('--case','back-center-fold','--channels','7','--option','--upmix3d-back-center=fold','--expect','Input layout 6.1')
        Invoke-IntegrationCase -CaseArguments @('--case','per-input-preset','--channels','6','--option','--upmix3d-preset-5-1=movie','--expect','effective preset movie')
        Invoke-IntegrationCase -CaseArguments @('--case','preset-edit','--option','--upmix3d-preset=movie','--edit-preset','--expect','effective preset custom')
        Invoke-IntegrationCase -CaseArguments @('--case','preset-reload','--read-config','--expect','effective preset custom')
        Invoke-IntegrationCase -CaseArguments @('--case','preset-dialogue','--option','--no-upmix3d-dialogue-protect','--expect','effective preset custom')
        Invoke-IntegrationCase -CaseArguments @('--case','preset-height-balance','--option','--upmix3d-height-balance=0.5','--expect','effective preset custom')
        Invoke-IntegrationCase -CaseArguments @('--case','device-selection','--select-device','--expect','route')
        Invoke-IntegrationCase -CaseArguments @('--case','stop-paused','--stop-paused')
        Invoke-IntegrationCase -CaseArguments @('--case','repeated-controls','--cycles','25')
        Invoke-IntegrationCase -CaseArguments @('--case','low-latency','--route','shared','--layout','7.1','--option','--upmix3d-latency=low','--expect','DSP delay 83 frames')
        Invoke-IntegrationCase -CaseArguments @('--case','high-quality','--option','--upmix3d-latency=quality','--expect','DSP delay 4179 frames')
        Invoke-IntegrationCase -CaseArguments @('--case','reset-settings','--no-controls','--option','--upmix3d-reset-settings','--expect','All upmix3d settings were reset')
        Invoke-IntegrationCase -CaseArguments @('--case','language-ja','--no-controls','--language','ja','--expect-language','ja')
        Invoke-IntegrationCase -CaseArguments @('--case','language-en','--no-controls','--language','fr','--expect-language','en')
        foreach ($entry in @(@('silence.ac3','AC-3'),@('silence.eac3','E-AC-3'),@('silence.dts','DTS'),@('silence.thd','TrueHD/MLP'))) {
            Invoke-IntegrationCase -CaseArguments @('--case', $entry[1].Replace('/','-'),'--input',"tests/fixtures/$($entry[0])",'--expect',"Bitstream passthrough $($entry[1])")
        }
        Invoke-IntegrationCase -CaseArguments @('--case','DTS-HD','--input','tests/fixtures/silence.dts','--option','--upmix3d-dts-hd','--expect','Bitstream passthrough DTS-HD')
        Invoke-IntegrationCase -CaseArguments @('--case','passthrough-disabled','--input','tests/fixtures/silence.eac3','--option','--upmix3d-passthrough=0','--expect','PCM input 5.1')
        Invoke-IntegrationCase -CaseArguments @('--case','passthrough-core-only','--input','tests/fixtures/silence.eac3','--option','--upmix3d-passthrough=1','--expect','PCM input 5.1')
    }
    $benchmark = Join-Path $projectPath "build\$Configuration\upmix3d_benchmark.exe"
    $duration = if ($LongDsp) { 7200 } else { 10 }
    $benchmarkOutput = & $benchmark $duration
    $benchmarkExit = $LASTEXITCODE
    $benchmarkOutput | Write-Output
    [IO.File]::WriteAllText((Join-Path $projectPath 'build\dsp-validation.json'), ($benchmarkOutput -join [Environment]::NewLine), [Text.UTF8Encoding]::new($false))
    if ($benchmarkExit -ne 0) { throw 'DSP endurance or performance validation failed' }
}
finally { Pop-Location }

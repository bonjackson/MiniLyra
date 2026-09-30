[CmdletBinding()]
param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) {
    throw "UnrealEditor-Cmd.exe was not found under $EngineRoot"
}
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $logDirectory, $cacheDirectory -Force | Out-Null
$logPath = Join-Path $logDirectory 'Task18-CuePathLifecycle.log'
if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    Remove-Item -LiteralPath $logPath
}

& $editor (Join-Path $projectRoot 'FPS.uproject') `
    '-ExecCmds=Mini.Task18CuePathLifecycle' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' `
    '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" `
    "-abslog=$logPath" | Out-Null

if ($LASTEXITCODE -ne 0 -or
    -not (Select-String -LiteralPath $logPath -SimpleMatch 'MiniTask18CuePath RESULT: PASS' -Quiet)) {
    throw "Task 18 GameFeature Cue lifecycle failed. See $logPath"
}
Write-Host "Task 18 GameFeature Cue lifecycle passed. Log: $logPath"

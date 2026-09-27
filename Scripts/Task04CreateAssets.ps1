[CmdletBinding()]
param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editor)) {
    throw "UnrealEditor-Cmd.exe was not found under $EngineRoot"
}
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$logPath = Join-Path $logDirectory 'Task04-CreateAssets.log'
if (Test-Path -LiteralPath $logPath) {
    Remove-Item -LiteralPath $logPath
}
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null

& $editor (Join-Path $projectRoot 'FPS.uproject') `
    '-run=pythonscript' "-script=$(Join-Path $PSScriptRoot 'Task04CreateAssets.py')" `
    '-EnablePlugins=PythonScriptPlugin' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' `
    '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" `
    "-abslog=$logPath" | Out-Null

if ($LASTEXITCODE -ne 0) {
    throw "Task 04 asset creation exited with code $LASTEXITCODE. See $logPath"
}
if (-not (Select-String -LiteralPath $logPath -SimpleMatch 'MINI_TASK04_ASSETS_CREATED' -Quiet)) {
    throw "Task 04 asset creation did not finish. See $logPath"
}
Write-Host "Task 04 assets created and practice map updated. Log: $logPath"

[CmdletBinding()]
param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) {
    throw "UnrealEditor-Cmd.exe was not found under $EngineRoot"
}

$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $logDirectory, $cacheDirectory -Force | Out-Null
$logPath = Join-Path $logDirectory 'Task07-VerifyAssets.log'
if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    Remove-Item -LiteralPath $logPath
}

& $editor $projectFile `
    '-run=pythonscript' "-script=$(Join-Path $PSScriptRoot 'Task07VerifyAssets.py')" `
    '-EnablePlugins=PythonScriptPlugin' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' `
    '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" `
    "-abslog=$logPath" | Out-Null

if ($LASTEXITCODE -ne 0) {
    throw "Task 07 saved-asset verification exited with code $LASTEXITCODE. See $logPath"
}
if (-not (Select-String -LiteralPath $logPath -SimpleMatch 'MINI_TASK07_ASSETS_VERIFIED' -Quiet)) {
    throw "Task 07 saved-asset verification did not finish. See $logPath"
}
Write-Host "Task 07 saved assets reloaded and verified. Log: $logPath"

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
$logPath = Join-Path $logDirectory 'Task10-CreateAssets.log'
if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    Remove-Item -LiteralPath $logPath
}

& $editor (Join-Path $projectRoot 'FPS.uproject') `
    '-run=pythonscript' "-script=$(Join-Path $PSScriptRoot 'Task10CreateAssets.py')" `
    '-EnablePlugins=PythonScriptPlugin' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' `
    '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" `
    "-abslog=$logPath" | Out-Null

if ($LASTEXITCODE -ne 0 -or
    -not (Select-String -LiteralPath $logPath -SimpleMatch 'MINI_TASK10_ASSETS_CREATED' -Quiet)) {
    throw "Task 10 asset creation failed. See $logPath"
}
Write-Host "Task 10 InputActions, InputConfig, mapping context, and links saved. Log: $logPath"

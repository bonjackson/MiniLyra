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

foreach ($stage in @('Create', 'Verify')) {
    $scriptPath = Join-Path $PSScriptRoot "Task18${stage}CueAssets.py"
    $logPath = Join-Path $logDirectory "Task18-${stage}CueAssets.log"
    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        Remove-Item -LiteralPath $logPath
    }
    & $editor (Join-Path $projectRoot 'FPS.uproject') `
        '-run=pythonscript' "-script=$scriptPath" `
        '-EnablePlugins=PythonScriptPlugin' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' `
        '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" `
        "-abslog=$logPath" | Out-Null
    $marker = if ($stage -eq 'Create') {
        'MINI_TASK18_CUE_ASSETS_CREATED'
    } else {
        'MINI_TASK18_CUE_ASSETS_VERIFIED'
    }
    if ($LASTEXITCODE -ne 0 -or
        -not (Select-String -LiteralPath $logPath -SimpleMatch $marker -Quiet)) {
        throw "Task 18 $stage Cue assets failed. See $logPath"
    }
    Write-Host "Task 18 $stage Cue assets passed. Log: $logPath"
}

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

# Two creation processes prove the action is updated in place; an independent
# verification process then reads serialized defaults without authoring them.
foreach ($stage in @(
    @{Name = 'Create'; Script = 'Task19CreateAssets.py'; Marker = 'MINI_TASK19_UI_ASSETS_CREATED'},
    @{Name = 'CreateAgain'; Script = 'Task19CreateAssets.py'; Marker = 'MINI_TASK19_UI_ASSETS_CREATED'},
    @{Name = 'Verify'; Script = 'Task19VerifyAssets.py'; Marker = 'MINI_TASK19_UI_ASSETS_VERIFIED'}
)) {
    $logPath = Join-Path $logDirectory "Task19-$($stage.Name)Assets.log"
    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        Remove-Item -LiteralPath $logPath
    }
    & $editor (Join-Path $projectRoot 'FPS.uproject') `
        '-run=pythonscript' "-script=$(Join-Path $PSScriptRoot $stage.Script)" `
        '-EnablePlugins=PythonScriptPlugin' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' `
        '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" `
        "-abslog=$logPath" | Out-Null
    if ($LASTEXITCODE -ne 0 -or
        -not (Select-String -LiteralPath $logPath -SimpleMatch $stage.Marker -Quiet)) {
        throw "Task 19 UI assets $($stage.Name) failed. See $logPath"
    }
    Write-Host "Task 19 UI assets $($stage.Name) passed. Log: $logPath"
}

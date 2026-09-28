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
$projectFile = Join-Path $projectRoot 'FPS.uproject'

foreach ($stage in @(
    @{Name = 'Create'; Script = 'Task11CreateCameraAssets.py'; Marker = 'MINI_TASK11_CAMERA_ASSETS_CREATED'},
    @{Name = 'Verify'; Script = 'Task11VerifyCameraAssets.py'; Marker = 'MINI_TASK11_CAMERA_ASSETS_VERIFIED'},
    @{Name = 'SpawnsCreate'; Script = 'Task11SetPracticeSpawns.py'; Marker = 'MINI_TASK11_PRACTICE_SPAWNS_CREATED'},
    @{Name = 'SpawnsVerify'; Script = 'Task11VerifyPracticeSpawns.py'; Marker = 'MINI_TASK11_PRACTICE_SPAWNS_VERIFIED'}
)) {
    $logPath = Join-Path $logDirectory "Task11-Camera$($stage.Name).log"
    if (Test-Path -LiteralPath $logPath -PathType Leaf) {
        Remove-Item -LiteralPath $logPath
    }
    & $editor $projectFile '-run=pythonscript' "-script=$(Join-Path $PSScriptRoot $stage.Script)" `
        '-EnablePlugins=PythonScriptPlugin' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' `
        '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" `
        "-abslog=$logPath" | Out-Null
    if ($LASTEXITCODE -ne 0 -or
        -not (Select-String -LiteralPath $logPath -SimpleMatch $stage.Marker -Quiet)) {
        throw "Task 11 camera asset $($stage.Name) failed. See $logPath"
    }
    Write-Host "Task 11 camera asset $($stage.Name) passed: $logPath"
}

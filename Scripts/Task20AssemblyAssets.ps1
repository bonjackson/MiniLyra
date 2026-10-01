[CmdletBinding()]
param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) { throw "Editor unavailable: $editor" }
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $logDirectory, $cacheDirectory -Force | Out-Null

# Run Training assets before this script; Loadout and map authoring follow it.
foreach ($stage in @(
    @{Name='Create'; Script='Task20AssemblyCreateAssets.py'; Marker='MINI_TASK20_ASSEMBLY_CREATED'},
    @{Name='CreateAgain'; Script='Task20AssemblyCreateAssets.py'; Marker='MINI_TASK20_ASSEMBLY_CREATED'},
    @{Name='Verify'; Script='Task20AssemblyVerifyAssets.py'; Marker='MINI_TASK20_ASSEMBLY_VERIFIED'}
)) {
    $logPath = Join-Path $logDirectory "Task20-Assembly-$($stage.Name).log"
    if (Test-Path -LiteralPath $logPath -PathType Leaf) { Remove-Item -LiteralPath $logPath }
    & $editor (Join-Path $projectRoot 'FPS.uproject') '-run=pythonscript' `
        "-script=$(Join-Path $PSScriptRoot $stage.Script)" '-EnablePlugins=PythonScriptPlugin' `
        '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' '-ddc=InstalledNoZenLocalFallback' `
        "-LocalDataCachePath=$cacheDirectory" "-abslog=$logPath" | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -SimpleMatch $stage.Marker -Quiet)) {
        throw "Task 20 assembly $($stage.Name) failed. See $logPath"
    }
    Write-Host "Task 20 assembly $($stage.Name) passed. Log: $logPath"
}

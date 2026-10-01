[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateSet('All', 'Create', 'CreateAgain', 'Verify')]
    [string]$Stage = 'All'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) { throw "Editor unavailable: $editor" }
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $logDirectory, $cacheDirectory -Force | Out-Null

# Build FPSEditor first. Every stage uses its own Editor process; Verify only
# loads saved assets, and CreateAgain demonstrates repeatable authoring.
foreach ($entry in @(
    @{Name='Create'; Script='Task21CreateAssets.py'; Marker='MINI_TASK21_ASSETS_CREATED'},
    @{Name='CreateAgain'; Script='Task21CreateAssets.py'; Marker='MINI_TASK21_ASSETS_CREATED'},
    @{Name='Verify'; Script='Task21VerifyAssets.py'; Marker='MINI_TASK21_ASSETS_VERIFIED'}
)) {
    if ($Stage -ne 'All' -and $entry.Name -ne $Stage) { continue }
    $logPath = Join-Path $logDirectory "Task21-Assets-$($entry.Name).log"
    if (Test-Path -LiteralPath $logPath -PathType Leaf) { Remove-Item -LiteralPath $logPath }
    & $editor (Join-Path $projectRoot 'FPS.uproject') '-run=pythonscript' `
        "-script=$(Join-Path $PSScriptRoot $entry.Script)" '-EnablePlugins=PythonScriptPlugin' `
        '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' '-ddc=InstalledNoZenLocalFallback' `
        "-LocalDataCachePath=$cacheDirectory" "-abslog=$logPath" | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $logPath -PathType Leaf) -or
        -not (Select-String -LiteralPath $logPath -SimpleMatch $entry.Marker -Quiet)) {
        throw "Task 21 assets $($entry.Name) failed. See $logPath"
    }
    Write-Host "Task 21 assets $($entry.Name) passed. Log: $logPath"
}

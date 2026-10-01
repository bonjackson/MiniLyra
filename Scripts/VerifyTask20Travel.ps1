[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 420
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$runner = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if ($PackagedExe) { $runner = [System.IO.Path]::GetFullPath($PackagedExe) }
$logName = if ($PackagedExe) { 'Task20-Packaged-Travel.log' } else { 'Task20-Travel.log' }
$log = Join-Path $projectRoot "Saved\Logs\$logName"
$cache = Join-Path $projectRoot 'DerivedDataCache'
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing game runner: $runner" }
New-Item -ItemType Directory -Path (Split-Path -Parent $log), $cache -Force | Out-Null
if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log }
$arguments = @()
if ($PackagedExe) { $arguments += '/Game/Mini/Maps/L_MiniPractice' }
else { $arguments += @(('"{0}"' -f $projectFile), '/Game/Mini/Maps/L_MiniPractice', '-game') }
$arguments += @(
    '-MiniProbeTask20Travel',
    '-nullrhi', '-nosound', '-unattended', '-nosplash', '-nop4', '-ddc=InstalledNoZenLocalFallback',
    ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log)
)
$process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
try {
    $pass = 'MiniTask20Travel PASS: Rounds=3 OpenLevel=3 ServerTravel=3 Cleanups=6 Worlds=7 OldASC=0 OldTargetWidgets=0 OldHUDListeners=0 OldTimerCallbacks=0 NewTargets=3 NewSupply=1'
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $process.Refresh()
        if ($process.HasExited) { throw "Travel process exited early: $log" }
        $text = if (Test-Path -LiteralPath $log -PathType Leaf) { [string](Get-Content -LiteralPath $log -Raw) } else { '' }
        if ($text -match 'MiniTask20Travel FAIL:|MiniAddActors (FAILED|INVALID_SCOPE):|Experience state .* -> Failed|Fatal error:|Assertion failed:') {
            throw "Travel lifecycle failure: $log"
        }
        if ($text.Contains($pass)) { break }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $text.Contains($pass)) { throw "Task 20 travel did not PASS: $log" }
    $previous = -1
    for ($travel = 1; $travel -le 6; $travel++) {
        $round = [int][Math]::Ceiling($travel / 2)
        $method = if ($travel % 2 -eq 1) { 'OpenLevel' } else { 'ServerTravel' }
        foreach ($marker in @(
            "MiniTask20Travel REQUEST: Travel=$travel Round=$round Method=$method TargetResetPending=1 SupplyOverlap=1 MenuOpen=1",
            "MiniTask20Travel OLD_WORLD_RELEASED: Travel=$travel ActionActors=0 PendingLoads=0 BeginPlayBindings=0 TargetASC=0 TargetWidgets=0 HUDWidgets=0 HUDListeners=0 VM=0 MenuBlocks=0",
            "MiniTask20Travel OLD_CALLBACKS_SILENT: Travel=$travel WaitOverResetDelay=1 TargetResetCallbacks=0 SupplyRefillCallbacks=0 HUDListeners=0",
            "MiniTask20Travel WORLD_READY: Generation=$($travel + 1) Targets=3 Supplies=1 ActionActors=4 PendingLoads=0 NewTargetStates=1 NewOwnerHUD=1"
        )) {
            $index = $text.IndexOf($marker, [StringComparison]::Ordinal)
            if ($index -le $previous) { throw "Missing/out-of-order '$marker': $log" }
            $previous = $index
        }
    }
    Write-Host 'Task 20 travel PASS: three rounds of real OpenLevel plus non-seamless ServerTravel; six old worlds release actors/ASC/labels/HUD/menu listeners, old timer callbacks remain silent, and seven gameplay worlds each have exactly three new targets and one supply.'
} finally {
    $process.Refresh()
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
}

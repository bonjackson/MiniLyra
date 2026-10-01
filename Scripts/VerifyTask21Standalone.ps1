[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [ValidateRange(30, 600)][int]$TimeoutSeconds = 120
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$runner = if ($PackagedExe) { [IO.Path]::GetFullPath($PackagedExe) } else { Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing runner: $runner" }
$logs = Join-Path $projectRoot 'Saved\Logs'
$cache = Join-Path $projectRoot 'DerivedDataCache'
$prefix = if ($PackagedExe) { 'Task21-Packaged' } else { 'Task21' }
New-Item -ItemType Directory -Path $logs, $cache -Force | Out-Null
foreach ($mode in 'Cancel', 'Practice') {
    $log = Join-Path $logs "$prefix-$mode.log"
    if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log }
    $url = '/Game/Mini/Maps/L_MiniPractice'
    if ($mode -eq 'Cancel') { $url += '?Experience=DA_MiniArenaDiagnosticsExperience' }
    $arguments = @()
    if (-not $PackagedExe) { $arguments += ('"{0}"' -f (Join-Path $projectRoot 'FPS.uproject')) }
    $arguments += $url
    if (-not $PackagedExe) { $arguments += '-game' }
    $arguments += @("-MiniProbeTask21=$mode", '-nullrhi', '-nosound', '-unattended', '-nosplash', '-nop4',
        '-ddc=InstalledNoZenLocalFallback', ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log))
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    try {
        $pass = if ($mode -eq 'Cancel') {
            'MiniTask21Probe CANCEL_PASS: Specs=0 Timer=0 Pending=0 OldDeadlinePassed=1 NoAdvance=1 SameTagIdempotent=1 InvalidRequestPreservesPhase=1'
        } else {
            'MiniTask21Probe PRACTICE_PASS: Rules=0 Phase=0 Specs=0 PhaseTimer=0 HUDCountdown=0 PlayerPhaseSpecs=0 Weapons=2 Rifle=30/90 HUD=1 Input=1 Targets=3 Supply=1'
        }
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        do {
            $process.Refresh()
            if ($process.HasExited) { throw "$mode exited early: $log" }
            $text = if (Test-Path -LiteralPath $log -PathType Leaf) { [string](Get-Content -LiteralPath $log -Raw) } else { '' }
            if ($text -match 'MiniTask21Probe FAIL:|Experience state .* -> Failed|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID') { throw "$mode failed: $log" }
            if ($text.Contains($pass)) { break }
            Start-Sleep -Milliseconds 250
        } while ([DateTime]::UtcNow -lt $deadline)
        if (-not $text.Contains($pass)) { throw "$mode did not PASS: $log" }
        if ($mode -eq 'Cancel' -and -not $text.Contains('MiniTask21Probe SERVER_CANCEL: BeforeSpecs=1 BeforeTimer=1')) {
            throw "Cancel lacked an actual active phase/timer before cancellation: $log"
        }
        Write-Host "Task 21 $mode PASS. Log: $log"
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}

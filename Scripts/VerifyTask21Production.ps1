[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(30, 300)][int]$TimeoutSeconds = 90
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (Test-Path -LiteralPath (Join-Path $projectRoot 'Plugins\GameFeatures\MiniArena\Content\Config\DA_MiniFFAMatchRules.uasset')) {
    # Task22 replaces the production timed demo with a real two-player FFA.
    & (Join-Path $PSScriptRoot 'VerifyTask22Production.ps1') -EngineRoot $EngineRoot -TimeoutSeconds $TimeoutSeconds
    return
}
$runner = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$log = Join-Path $projectRoot 'Saved\Logs\Task21-Production.log'
if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
$arguments = @(
    ('"{0}"' -f (Join-Path $projectRoot 'FPS.uproject')),
    '/Game/Mini/Maps/L_MiniPractice?Experience=DA_MiniArenaExperience', '-game',
    '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
    '-ddc=InstalledNoZenLocalFallback',
    ('"-LocalDataCachePath={0}"' -f (Join-Path $projectRoot 'DerivedDataCache')),
    ('"-abslog={0}"' -f $log)
)
$process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    do {
        $process.Refresh()
        if ($process.HasExited) { throw "Production entry exited early: $log" }
        $text = if (Test-Path -LiteralPath $log) { [string](Get-Content -LiteralPath $log -Raw) } else { '' }
        if ($text -match 'Experience state .* -> Failed|MiniPhase .*REJECTED:|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID') {
            throw "Production arena entry failed: $log"
        }
        $warmup = [regex]::Match($text, 'MiniPhase BEGIN: .*Tag=GamePhase.MiniArena.Warmup .*Deadline=(\d+\.\d+)')
        $playing = [regex]::Match($text, 'MiniPhase BEGIN: .*Tag=GamePhase.MiniArena.Playing .*Deadline=(\d+\.\d+)')
        if ($warmup.Success -and $playing.Success -and $text.Contains('MiniSpawn COMMITTED:')) { break }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $warmup.Success -or -not $playing.Success -or $playing.Index -le $warmup.Index -or
        -not $text.Contains('ExperienceId=MiniExperienceDefinition:DA_MiniArenaExperience') -or
        $text.Contains('MiniTask21Probe ') -or $text.Contains('DA_MiniArenaDiagnosticsExperience')) {
        throw "Production URL/config did not drive the actual arena: $log"
    }
    # The Playing deadline is 60 seconds later than the Warmup deadline,
    # without any diagnostic timer override.
    $interval = [double]::Parse($playing.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) -
        [double]::Parse($warmup.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
    if ([Math]::Abs($interval - 60.0) -gt 0.25) { throw "Production Playing deadline is incorrect: interval=$interval" }
    Write-Host "Task 21 production entry PASS: normal URL, plugin Blueprint/config, Warmup -> Playing, 60-second Playing deadline, no probe/diagnostic overrides. Log: $log"
} finally {
    $process.Refresh()
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
}

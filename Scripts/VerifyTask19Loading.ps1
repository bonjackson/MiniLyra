[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 420,
    [ValidateRange(1024, 65535)][int]$Port = 18929,
    [switch]$WithMedia
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logs = Join-Path $projectRoot 'Saved\Logs'
$cache = Join-Path $projectRoot 'DerivedDataCache'
$screenshots = Join-Path $projectRoot 'Saved\Screenshots'
$prefix = if ($WithMedia) { 'Task19-Loading-WithMedia' } else { 'Task19-Loading' }
$validId = 'MiniExperienceDefinition:DA_MiniPracticeExperience'
$invalidId = 'MiniExperienceDefinition:DA_MiniDefinitelyMissing'

if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) { throw "Missing editor: $editor" }
if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) { throw "Missing project: $projectFile" }
New-Item -ItemType Directory -Path $logs, $cache, $screenshots -Force | Out-Null

function Start-LoadingProbe([string]$Scenario, [string]$Role, [bool]$InvalidExperience) {
    $log = Join-Path $logs "$prefix-$Scenario-$Role.log"
    if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log }
    $url = if ($Role -eq 'Server') { '/Game/Mini/Maps/L_MiniPractice?listen' } else { "127.0.0.1:$Port" }
    $arguments = @(
        ('"{0}"' -f $projectFile), $url, '-game', '-MiniProbeExperienceFlow', '-MiniProbeTask19Loading',
        '-unattended', '-nosplash', '-nop4', '-nosound', '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log)
    )
    if ($Role -eq 'Server') { $arguments += "-port=$Port" }
    if ($InvalidExperience) { $arguments += '-MiniProbeInvalidExperience' }
    if ($WithMedia) {
        $arguments += @('-MiniProbeTask19LoadingMedia', '-windowed', '-ResX=960', '-ResY=540', '"-ExecCmds=t.MaxFPS 30"')
    } else {
        $arguments += '-nullrhi'
    }
    # Do not run the combat/UI actor probe here: an invalid Experience cannot
    # spawn the Pawn that its three-process checkpoints legitimately require.
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 19 loading $Scenario $Role started: PID=$($process.Id), log=$log"
    return @{ Process = $process; Role = $Role; Log = $log }
}

function Read-LoadingLog($Entry, [bool]$InvalidExperience) {
    $Entry.Process.Refresh()
    if ($Entry.Process.HasExited) {
        throw "$($Entry.Role) exited before loading UI verification (exit code $($Entry.Process.ExitCode)): $($Entry.Log)"
    }
    $text = if (Test-Path -LiteralPath $Entry.Log -PathType Leaf) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($text -match 'MiniTask19Loading FAIL:|MiniFlowProbe FAIL:|Fatal error:|Assertion failed:') {
        throw "$($Entry.Role) reported a probe/runtime failure: $($Entry.Log)"
    }
    if (-not $InvalidExperience -and $text -match 'Experience state .* -> Failed') {
        throw "Valid Experience failed: $($Entry.Log)"
    }
    return $text
}

function Test-LoadingTerminal([string]$Text, [string]$NetMode, [bool]$InvalidExperience) {
    if ($InvalidExperience) {
        $pattern = 'MiniTask19Loading PASS: NetMode=' + $NetMode +
            ' State=Failed Visible=1 Failure=1 TitlePresent=1 DetailMatches=1 UIBlocks=1 ControllerBlocked=1 TickerStopped=1 PendingObserved=[01] Experience=' +
            [regex]::Escape($invalidId) + ' Reason=Unknown Experience ID'
        $flow = '(?m)MiniFlowProbe FAIL_EXPECTED: NetMode=' + $NetMode + '\b.*Unknown Experience ID'
        $terminalMatches = $Text -match $pattern -and $Text -match $flow -and
            $Text.Contains('LoadingAssets -> Failed Reason=Unknown Experience ID')
        if ($NetMode -eq 'Client') {
            $terminalMatches = $terminalMatches -and $Text.Contains("Experience ID replicated NetMode=Client ID=$invalidId")
        }
        return $terminalMatches
    }
    $pattern = 'MiniTask19Loading PASS: NetMode=' + $NetMode +
        ' State=Loaded Visible=0 Failure=0 TitleEmpty=1 DetailEmpty=1 UIBlocks=0 ControllerBlocked=0 TickerStopped=1 PendingObserved=[01] Experience=' +
        [regex]::Escape($validId) + '\b'
    $terminalMatches = $Text -match $pattern -and
        $Text.Contains("MiniFlowProbe PASS: NetMode=$NetMode ID=$validId LateSubscriber=1") -and
        $Text.Contains("ID=$validId ExecutingActions -> Loaded")
    if ($NetMode -eq 'ListenServer') {
        $terminalMatches = $terminalMatches -and $Text.Contains("MiniGameMode selected map Experience $validId")
    } else {
        $terminalMatches = $terminalMatches -and $Text.Contains("Experience ID replicated NetMode=Client ID=$validId")
    }
    return $terminalMatches
}

function Invoke-LoadingScenario([string]$Scenario, [bool]$InvalidExperience) {
    $state = if ($InvalidExperience) { 'Failed' } else { 'Loaded' }
    $evidence = @('ListenServer', 'Client' | ForEach-Object {
        Join-Path $screenshots "Task19-Loading-$state-$_.png"
    })
    if ($WithMedia) {
        foreach ($path in $evidence) {
            if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path }
        }
    }
    $entries = @()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    try {
        $server = Start-LoadingProbe $Scenario 'Server' $InvalidExperience
        $entries += $server
        $listener = '(?im)\bLogNet:.*\blistening on port\s+' + $Port + '\b'
        do {
            $serverText = Read-LoadingLog $server $InvalidExperience
            if ($serverText -match $listener) { break }
            Start-Sleep -Milliseconds 500
        } while ([DateTime]::UtcNow -lt $deadline)
        if ($serverText -notmatch $listener) { throw "$Scenario listen server did not open port ${Port}: $($server.Log)" }

        $client = Start-LoadingProbe $Scenario 'Client' $InvalidExperience
        $entries += $client
        do {
            $serverText = Read-LoadingLog $server $InvalidExperience
            $clientText = Read-LoadingLog $client $InvalidExperience
            $serverPassed = Test-LoadingTerminal $serverText 'ListenServer' $InvalidExperience
            $clientPassed = Test-LoadingTerminal $clientText 'Client' $InvalidExperience
            if ($serverPassed -and $clientPassed) { break }
            Start-Sleep -Milliseconds 500
        } while ([DateTime]::UtcNow -lt $deadline)
        if (-not $serverPassed -or -not $clientPassed) {
            throw "$Scenario actual loading UI timed out (server=$serverPassed client=$clientPassed). Logs: $($server.Log); $($client.Log)"
        }
        if ($WithMedia) {
            foreach ($path in $evidence) {
                if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 1024) {
                    throw "Missing actual UMG screenshot: $path"
                }
            }
            foreach ($entry in $entries) {
                $text = Read-LoadingLog $entry $InvalidExperience
                if ($text -notmatch ('MiniTask19Loading SCREENSHOT_REQUESTED: NetMode=(ListenServer|Client) State=' + $state + ' ShowUI=1')) {
                    throw "UI-inclusive screenshot was not requested: $($entry.Log)"
                }
            }
        }
        Write-Host "Task 19 loading $Scenario PASS: both processes verified manager terminal state, actual attached UMG visibility/text, controller/root gate and stopped status ticker."
    } finally {
        # Preserve every other UE session; stop only this script's own handles.
        foreach ($entry in $entries) {
            try {
                $entry.Process.Refresh()
                if (-not $entry.Process.HasExited) {
                    Stop-Process -Id $entry.Process.Id -Force -ErrorAction Stop
                    $entry.Process.WaitForExit(10000) | Out-Null
                }
            } catch [System.InvalidOperationException] {
                # It exited between Refresh and Stop-Process.
            }
        }
    }
}

Invoke-LoadingScenario 'Valid' $false
Invoke-LoadingScenario 'Invalid' $true
if ($WithMedia) {
    Write-Host 'Four UI-inclusive Loaded/Failed screenshots are under Saved\Screenshots. Inspect Failed screenshots visually before claiming rendered error UI quality.'
}
Write-Host 'Task 19 loading verification PASS: Loaded hides status and releases UI input gate; a real unknown Experience shows its exact failure reason and retains the gate; both terminal states stop the status ticker.'

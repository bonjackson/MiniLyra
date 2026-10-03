[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [ValidateSet('All', 'Flow', 'Failures', 'Capacity', 'HostLoss', 'Quit', 'HostFailures')][string]$Mode = 'All',
    [ValidateRange(120, 1800)][int]$TimeoutSeconds = 420,
    [ValidateRange(1024, 65534)][int]$Port = 18924,
    [switch]$WithMedia
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$runner = if ($PackagedExe) { [IO.Path]::GetFullPath($PackagedExe) } else {
    Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
}
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing runner: $runner" }
$logs = Join-Path $projectRoot 'Saved\Logs'
$cache = Join-Path $projectRoot 'DerivedDataCache'
$screenshots = Join-Path $projectRoot 'Saved\Screenshots'
New-Item -ItemType Directory -Path $logs, $cache, $screenshots -Force | Out-Null
$prefix = if ($PackagedExe) { 'Task24-Packaged' } else { 'Task24' }
if ($WithMedia) { $prefix += '-WithMedia' }

function Start-Task24Peer([string]$Scenario, [string]$Role, [string]$Name, [string]$SignalDir) {
    $log = Join-Path $logs "$prefix-$Scenario-$Name.log"
    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log }
    $arguments = @()
    if (-not $PackagedExe) { $arguments += ('"{0}"' -f (Join-Path $projectRoot 'FPS.uproject')) }
    # No URL or Experience option: every peer starts at the actual default menu.
    if (-not $PackagedExe) { $arguments += '-game' }
    $arguments += @("-MiniProbeTask24=$Scenario", "-MiniTask24Role=$Role", "-MiniTask24Peer=$Name",
        "-MiniTask24Address=127.0.0.1:$Port", "-MiniTask24UnusedAddress=127.0.0.1:$($Port + 1)",
        ('"-MiniTask24SignalDir={0}"' -f $SignalDir), "-MiniTask24TimeoutSeconds=$TimeoutSeconds",
        '-unattended', '-nosplash', '-nop4', '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log))
    # Ordinary ServerTravel gives the host a four-second departure delay. Keep
    # enough time for that and the new handshake; only failure scenarios use 5s.
    $connectionTimeout = if ($Scenario -in @('Failures', 'HostLoss')) { 5.0 } else { 30.0 }
    $arguments += "-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:InitialConnectTimeout=$connectionTimeout,ConnectionTimeout=$connectionTimeout"
    if ($Role -eq 'Server') { $arguments += "-port=$Port" }
    if ($Scenario -eq 'HostFailureListen') {
        $arguments += @('-MULTIHOME=127.0.0.1', '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:MaxPortCountToTry=0,bExitOnBindFailure=False')
    }
    if ($Scenario -eq 'HostFailureCreate') {
        # Preserve inner quotes through the Windows argv parser: UE's FParse
        # otherwise ends the value at the first comma and keeps the real fallback.
        $arguments += '-NetDriverOverrides=\"GameNetDriver,/Script/FPS.Task24MissingNetDriver,/Script/FPS.Task24MissingNetDriver\"'
    }
    if ($WithMedia) {
        $arguments += @('-MiniProbeTask24Media', ('"-MiniProbeTask24MediaOutput={0}"' -f $screenshots),
            '-windowed', '-ResX=960', '-ResY=540',
            '"-ExecCmds=t.MaxFPS 30,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1,r.MotionBlurQuality 0"')
    } else { $arguments += @('-nullrhi', '-nosound') }
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task24 $Scenario $Name started. PID=$($process.Id) Log=$log"
    return @{Process=$process; Log=$log; Role=$Role; Name=$Name; ExpectedKilled=$false; ExpectedQuit=($Scenario -eq 'Quit')}
}
function Read-Task24Healthy($Entry) {
    $Entry.Process.Refresh()
    $text = if (Test-Path -LiteralPath $Entry.Log) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($text -match 'MiniTask24Probe FAIL:|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID|MiniAddWidgets (LOAD_FAILED|INVALID_CLASS_OR_TAG|NO_LAYER|NO_EXTENSION_SUBSYSTEM):') {
        throw "Task24 $($Entry.Name) failed: $($Entry.Log)"
    }
    if ($Entry.Process.HasExited -and -not $Entry.ExpectedKilled -and
        (-not $Entry.ExpectedQuit -or $Entry.Process.ExitCode -ne 0 -or -not $text.Contains('MiniTravel QUIT: FrontEnd=1 PendingRequest=0'))) {
        throw "Task24 $($Entry.Name) exited early (code $($Entry.Process.ExitCode)): $($Entry.Log)"
    }
    return $text
}
function Wait-Task24GracefulExit($Entry, $Entries, [DateTime]$Deadline, [bool]$WasInGame) {
    do {
        foreach ($peer in $Entries) { $null = Read-Task24Healthy $peer }
        [string]$text = Read-Task24Healthy $Entry
        $Entry.Process.Refresh()
        if ($Entry.Process.HasExited) {
            if ($WasInGame -and $text -notmatch 'MiniTravel READY: Operation=6 .*World=L_MiniFrontEnd .*FrontEnd=1') {
                throw "Gameplay Quit did not complete real return travel before process exit: $($Entry.Log)"
            }
            Write-Host "Task24 actual graceful Quit. Peer=$($Entry.Name) PID=$($Entry.Process.Id) ExitCode=$($Entry.Process.ExitCode)"
            return $text
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw "Product Quit did not terminate its process: $($Entry.Log)"
}
function Wait-Task24Marker($Entry, $Entries, [string]$Marker, [DateTime]$Deadline) {
    do {
        foreach ($peer in $Entries) { $null = Read-Task24Healthy $peer }
        [string]$text = Read-Task24Healthy $Entry
        if ($text.Contains("MiniTask24Probe $Marker")) { return $text }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw "Task24 missing '$Marker': $($Entry.Log)"
}
function Assert-Task24UIEvidence($Entry) {
    [string]$text = Read-Task24Healthy $Entry
    if (-not $text.Contains('MiniTask24Probe UI_CLICK:')) {
        throw "Task24 peer did not route a real Slate button click: $($Entry.Log)"
    }
    if ($Entry.Role -ne 'Server' -and $Entry.Name -ne 'FrontEndExit' -and -not $text.Contains('MiniTask24Probe ADDRESS_TYPED:')) {
        throw "Task24 client did not type an address through Slate: $($Entry.Log)"
    }
    if ($WithMedia -and -not $text.Contains('MiniTask24Probe SCREENSHOT_SAVED:')) {
        throw "Task24 media peer did not observe an actual screenshot: $($Entry.Log)"
    }
}
[string[]]$scenarios = if ($Mode -eq 'All') { @('Flow', 'Failures', 'Capacity', 'HostLoss', 'Quit', 'HostFailureListen', 'HostFailureCreate') }
    elseif ($Mode -eq 'HostFailures') { @('HostFailureListen', 'HostFailureCreate') } else { @($Mode) }
foreach ($scenario in $scenarios) {
    $entries = @()
    $occupiedSocket = $null
    $signalDir = Join-Path $projectRoot ("Saved\Task24Signals\{0}-{1}" -f $scenario, [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $signalDir -Force | Out-Null
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        if ($scenario -eq 'HostFailureListen') {
            $occupiedSocket = [Net.Sockets.Socket]::new([Net.Sockets.AddressFamily]::InterNetwork, [Net.Sockets.SocketType]::Dgram, [Net.Sockets.ProtocolType]::Udp)
            $occupiedSocket.ExclusiveAddressUse = $true
            $occupiedSocket.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Parse('127.0.0.1'), $Port))
        }
        $server = Start-Task24Peer $scenario 'Server' 'Server' $signalDir
        $entries += $server
        if ($scenario -in @('HostFailureListen', 'HostFailureCreate')) {
            $null = Wait-Task24Marker $server $entries 'HOST_FAILURE_MODAL' $deadline
            if ($occupiedSocket) {
                $occupiedSocket.Dispose(); $occupiedSocket = $null
                Set-Content -LiteralPath (Join-Path $signalDir 'HostFailurePortReleased.signal') -Value 'Exclusive test socket disposed; product Host may retry.' -Encoding utf8
            }
            $marker = if ($scenario -eq 'HostFailureListen') { 'HOST_FAILURE_LISTEN_PASS' } else { 'HOST_FAILURE_CREATE_PASS' }
            $text = Wait-Task24Marker $server $entries $marker $deadline
            $failure = if ($scenario -eq 'HostFailureListen') { 'NetDriverListenFailure' } else { 'NetDriverCreateFailure' }
            if ($text -notmatch ("BroadcastNetworkFailure:.*" + $failure) -or -not $text.Contains('Code=MINI_HOST_FAILED')) {
                throw "Host failure did not use the actual engine driver failure: $scenario"
            }
            if ($scenario -eq 'HostFailureCreate' -and $text -notmatch 'HOST_FAILURE_DETAIL: Type=1 Driver=None') {
                throw 'CreateFailure did not exercise the actual null-driver branch.'
            }
            Assert-Task24UIEvidence $server
            Write-Host "Task24 $scenario PASS: actual driver initialization failure, prompt reason, menu recovery and usable next operation."
            continue
        }
        $null = Wait-Task24Marker $server $entries 'SERVER_HOST_READY' $deadline
        if ($scenario -eq 'Capacity') {
            foreach ($name in 'ClientA', 'ClientB', 'ClientC') {
                $entries += Start-Task24Peer $scenario 'Client' $name $signalDir
            }
            $null = Wait-Task24Marker $server $entries 'SERVER_CAPACITY_FOUR_READY' $deadline
            $overflow = Start-Task24Peer $scenario 'Overflow' 'Overflow' $signalDir
            $entries += $overflow
            $null = Wait-Task24Marker $server $entries 'CAPACITY_SERVER_PASS' $deadline
            $null = Wait-Task24Marker $overflow $entries 'CAPACITY_OVERFLOW_PASS' $deadline
            $retained = 0
            $released = 0
            foreach ($entry in $entries | Where-Object Role -eq 'Client') {
                $text = Read-Task24Healthy $entry
                if ($text.Contains('MiniTask24Probe CAPACITY_RETAINED_CLIENT_PASS')) { $retained++ }
                if ($text.Contains('MiniTask24Probe CAPACITY_RELEASE_CLIENT_PASS')) { $released++ }
            }
            if ($retained -ne 2 -or $released -ne 1) { throw "Capacity expected 2 retained and 1 released clients, got $retained/$released" }
            if (-not (Read-Task24Healthy $overflow).Contains('Code=MINI_SERVER_FULL') -or
                -not (Read-Task24Healthy $server).Contains('MiniLogin REJECTED: Code=MINI_SERVER_FULL Limit=4')) {
                throw 'Capacity test did not contain the real server approval rejection and client reason.'
            }
            Write-Host 'Task24 Capacity PASS: four accepted, fifth rejected, one real menu leave and overflow rejoin.'
        } else {
            $client = Start-Task24Peer $scenario 'Client' 'Client' $signalDir
            $entries += $client
            switch ($scenario) {
                'Flow' {
                    $null = Wait-Task24Marker $server $entries 'FLOW_SERVER_PASS' $deadline
                    $null = Wait-Task24Marker $client $entries 'FLOW_CLIENT_PASS' $deadline
                    if (-not (Read-Task24Healthy $client).Contains('Code=MINI_HOST_LEFT')) {
                        throw 'Flow did not preserve the real host-return reason.'
                    }
                    Write-Host 'Task24 Flow PASS: menu create/join, leave/rejoin, ordinary restart, host end, training and return.'
                }
                'Failures' {
                    $null = Wait-Task24Marker $server $entries 'FAILURES_SERVER_PASS' $deadline
                    $null = Wait-Task24Marker $client $entries 'FAILURES_CLIENT_PASS' $deadline
                    $text = Read-Task24Healthy $client
                    if (-not $text.Contains('Code=MINI_INVALID_ADDRESS') -or
                        -not $text.Contains('MiniTask24Probe CANCEL_PENDING_PASS') -or
                        -not $text.Contains('MiniTask24Probe PENDING_RETRY_PASS') -or
                        $text -notmatch 'BroadcastNetworkFailure:.*(ConnectionTimeout|PendingConnectionFailure)') {
                        throw 'Failures did not include invalid format, real pending cancellation/retry and a real engine pending connection failure.'
                    }
                    Write-Host 'Task24 Failures PASS: invalid input, pending cancellation/retry and real unreachable address recover to a successful menu join.'
                }
                'HostLoss' {
                    $null = Wait-Task24Marker $server $entries 'HOST_LOSS_ARMED' $deadline
                    $null = Wait-Task24Marker $client $entries 'HOST_LOSS_ARMED' $deadline
                    $server.ExpectedKilled = $true
                    Stop-Process -Id $server.Process.Id -Force
                    $server.Process.WaitForExit(5000) | Out-Null
                    $server.Process.Refresh()
                    if (-not $server.Process.HasExited) { throw 'Tracked host process remained alive after force termination.' }
                    Write-Host "Task24 HostLoss actual host terminated. PID=$($server.Process.Id)"
                    $null = Wait-Task24Marker $client $entries 'HOSTLOSS_CLIENT_PASS' $deadline
                    $text = Read-Task24Healthy $client
                    if ($text -notmatch 'BroadcastNetworkFailure:.*(ConnectionLost|ConnectionTimeout)' -or
                        $text -notmatch 'Code=MINI_CONNECTION_(LOST|TIMEOUT)') {
                        throw 'HostLoss must use the real driver failure and persistent client reason.'
                    }
                    Write-Host 'Task24 HostLoss PASS: real terminated host, client driver failure, reason and actionable front-end.'
                }
                'Quit' {
                    $null = Wait-Task24Marker $client $entries 'QUIT_CLIENT_REQUESTED' $deadline
                    $null = Wait-Task24GracefulExit $client $entries $deadline $true
                    $null = Wait-Task24Marker $server $entries 'QUIT_SERVER_READY_AFTER_CLIENT_EXIT' $deadline
                    Set-Content -LiteralPath (Join-Path $signalDir 'QuitServer.signal') -Value 'Client process exited through product Quit.' -Encoding utf8
                    $null = Wait-Task24Marker $server $entries 'QUIT_SERVER_REQUESTED' $deadline
                    $null = Wait-Task24GracefulExit $server $entries $deadline $true
                    $frontEndExit = Start-Task24Peer $scenario 'Overflow' 'FrontEndExit' $signalDir
                    $entries += $frontEndExit
                    $null = Wait-Task24Marker $frontEndExit $entries 'QUIT_FRONTEND_REQUESTED' $deadline
                    $null = Wait-Task24GracefulExit $frontEndExit $entries $deadline $false
                    Write-Host 'Task24 Quit PASS: client/host game menus return before graceful process exit; default front-end Quit exits directly.'
                }
            }
        }
        foreach ($entry in $entries) { Assert-Task24UIEvidence $entry }
    } finally {
        if ($occupiedSocket) { $occupiedSocket.Dispose() }
        foreach ($entry in $entries) {
            $entry.Process.Refresh()
            if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force }
        }
    }
}
if ($WithMedia) { Write-Host "Actual rendered evidence: $screenshots. Inspect it before visual acceptance." }

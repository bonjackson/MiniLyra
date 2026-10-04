[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [ValidateSet('All', 'Normal', 'ActorClassFailure', 'LateUIFailure')][string]$Mode = 'All',
    [ValidateSet('Both', 'Listen', 'Dedicated')][string]$ServerKind = 'Both',
    [ValidateRange(120, 900)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65534)][int]$Port = 18936,
    [switch]$WithMedia,
    [string]$ProjectRoot = ''
)
$ErrorActionPreference = 'Stop'
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$projectPath = [IO.Path]::GetFullPath($ProjectRoot)
$projectFile = Join-Path $projectPath 'FPS.uproject'
$runner = if ($PackagedExe) { [IO.Path]::GetFullPath($PackagedExe) } else { Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing Development runner: $runner" }
if (-not $PackagedExe -and -not (Test-Path -LiteralPath $projectFile -PathType Leaf)) { throw "Missing project: $projectFile" }
$logs = Join-Path $projectPath 'Saved\Logs'
$cache = Join-Path $projectPath 'DerivedDataCache'
$media = Join-Path $projectPath 'Saved\Screenshots'
$session = [guid]::NewGuid().ToString('N')
$signals = Join-Path $projectPath "Saved\Task26Roles\$session"
New-Item -ItemType Directory -Path $logs, $cache, $media, $signals -Force | Out-Null
$prefix = if ($WithMedia) { 'Task26Roles-WithMedia' } else { 'Task26Roles' }

function Start-RolePeer([string]$Scenario, [string]$Role, [string]$Peer, [string]$FinishFile, [string]$InjectFile) {
    $log = Join-Path $logs "$prefix-$Scenario-$Peer.log"
    if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log }
    $arguments = @()
    if (-not $PackagedExe) { $arguments += ('"{0}"' -f $projectFile) }
    if ($Role -eq 'Standalone') { $arguments += '/Game/Mini/Maps/L_MiniPractice' }
    elseif ($Role -eq 'Listen') { $arguments += '/Game/Mini/Maps/L_MiniPractice?listen' }
    elseif ($Role -eq 'Dedicated') { $arguments += '/Game/Mini/Maps/L_MiniPractice' }
    else { $arguments += "127.0.0.1:$Port" }
    if (-not $PackagedExe) { $arguments += '-game' }
    if ($Role -eq 'Dedicated') { $arguments += '-server' }
    if ($Role -in @('Listen', 'Dedicated')) { $arguments += "-port=$Port" }
    $arguments += @("-MiniProbeTask26Role=$Role", "-MiniTask26RoleScenario=$Scenario", "-MiniTask26RolePeer=$Peer",
        ('"-MiniTask26RoleFinishFile={0}"' -f $FinishFile), ('"-MiniTask26RoleInjectFile={0}"' -f $InjectFile),
        "-MiniTask26RoleTimeoutSeconds=$TimeoutSeconds", '-unattended', '-nosplash', '-nop4', '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log))
    if ($WithMedia -and $Role -ne 'Dedicated') {
        $arguments += @('-MiniTask26RoleMedia', ('"-MiniTask26RoleMediaOutput={0}"' -f $media), '-windowed', '-ResX=960', '-ResY=540',
            '"-ExecCmds=t.MaxFPS 30,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1,r.MotionBlurQuality 0"')
    } else { $arguments += @('-nullrhi', '-nosound') }
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectPath -WindowStyle Hidden -PassThru
    Write-Host "Task26 role $Scenario $Peer started. PID=$($process.Id) Log=$log"
    return @{ Process=$process; Log=$log; Role=$Role; Peer=$Peer; Scenario=$Scenario; Accepted=$false }
}
function Assert-RoleCount([string]$Text, [string]$Pattern, [int]$Expected, [string]$Label) {
    $count = [regex]::Matches($Text, $Pattern).Count
    if ($count -ne $Expected) { throw "$Label expected $Expected records, got $count." }
}
function Assert-RoleErrors([string]$Text, $Entry) {
    $scenario = $Entry.Scenario
    foreach ($errorLine in [regex]::Matches($Text, '(?m)^[^\r\n]*: Error:[^\r\n]*$')) {
        [string]$line = $errorLine.Value
        $allowed = $false
        if ($scenario -eq 'ActorClassFailure' -and $line -match 'LogMiniExperience: Error: Experience state .* -> Failed Reason=Required Action.*Mini/Diagnostics/Task26/MissingRoleActorClassFailure') { $allowed = $true }
        if ($scenario -eq 'LateUIFailure' -and $line -match 'LogMiniExperience: Error: Experience state .* -> Failed Reason=Required Action.*MiniTask19_AddWidgets.*required widget class or tag is invalid') { $allowed = $true }
        if ($Entry.Role -ne 'Client' -and $scenario -ne 'Normal' -and $line -match 'Log(Streaming|UObjectGlobals|Linker|PackageName): Error:' -and
            $line -match ('/Game/Mini/Diagnostics/Task26/MissingRole' + [regex]::Escape($scenario))) { $allowed = $true }
        if ($Entry.Role -eq 'Listen' -and $scenario -eq 'LateUIFailure' -and
            $line -match 'LogMiniInit: Error: MiniAddWidgets REQUIRED_FAILED:.*MiniTask19_AddWidgets.*Reason=required widget class or tag is invalid') { $allowed = $true }
        if (-not $allowed) { throw "Unexpected Error for $($Entry.Peer): $line" }
    }
}
function Read-RoleHealthy($Entry) {
    $Entry.Process.Refresh()
    [string]$text = if (Test-Path -LiteralPath $Entry.Log) { Get-Content -LiteralPath $Entry.Log -Raw } else { '' }
    # A live writer can append a long failure record in multiple writes. Judge
    # only complete lines; the next poll will validate the complete last record.
    $lastNewline = $text.LastIndexOf("`n")
    $text = if ($lastNewline -ge 0) { $text.Substring(0, $lastNewline + 1) } else { '' }
    if ($text -match 'MiniTask26Role FAIL:|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID|CreateSavedMove: Hit limit') {
        throw "Role runtime/probe failed: $($Entry.Log)"
    }
    if (-not $Entry.Accepted -and $text -match 'BroadcastNetworkFailure:|BroadcastTravelFailure:') { throw "Unexpected real network/travel failure: $($Entry.Log)" }
    if ($Entry.Process.HasExited -and (-not $Entry.Accepted -or $Entry.Process.ExitCode -ne 0)) { throw "Unexpected process exit: $($Entry.Peer) Exit=$($Entry.Process.ExitCode)" }
    Assert-RoleErrors $text $Entry
    return $text
}
function Wait-RoleMarkers($Entries, [string]$Marker, [DateTime]$Deadline) {
    do {
        $all = $true
        foreach ($entry in $Entries) {
            [string]$text = Read-RoleHealthy $entry
            if (-not $text.Contains("MiniTask26Role $Marker")) { $all = $false }
        }
        if ($all) { return }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $Deadline)
    throw "Task26 role matrix did not reach '$Marker' before deadline."
}
function Assert-RoleReady($Entry, [string]$Text) {
    Assert-RoleCount $Text 'MiniTask26Role READY:' 1 "$($Entry.Peer) real role Ready"
    Assert-RoleCount $Text 'MiniTask26Role RESOURCE_SCOPE:' 1 "$($Entry.Peer) resource role scope"
    if ($Entry.Role -eq 'Dedicated') {
        if ($Text -notmatch 'RESOURCE_SCOPE:.*Layouts=0 Elements=0 ActorClasses=[1-9]\d* SelectedGFDUI=0' -or
            $Text -notmatch 'READY:.*ActorsReady=1 AuthorityActors=[1-9]\d* ClientActorSkip=0 LocalUI=0 HUDWidgets=0 LocalInput=0') { throw 'Dedicated did not prove actual Actor readiness and UI skip.' }
    } elseif ($Entry.Role -eq 'Client') {
        if ($Text -notmatch 'RESOURCE_SCOPE:.*Layouts=[1-9]\d* Elements=4 ActorClasses=0 SelectedGFDUI=5' -or
            $Text -notmatch 'READY:.*ActorsReady=1 AuthorityActors=0 ClientActorSkip=1 LocalUI=1 HUDWidgets=4 LocalInput=1') { throw 'Client did not prove UI resources, local UI/input and authority Actor skip.' }
    } else {
        if ($Text -notmatch 'RESOURCE_SCOPE:.*Layouts=[1-9]\d* Elements=4 ActorClasses=[1-9]\d* SelectedGFDUI=5' -or
            $Text -notmatch 'READY:.*ActorsReady=1 AuthorityActors=[1-9]\d* ClientActorSkip=0 LocalUI=1 HUDWidgets=4 LocalInput=1') { throw 'Authority local role did not prove selected GFD UI and actual Actor/UI readiness.' }
    }
    if ($Entry.Role -ne 'Dedicated' -and $Text -notmatch 'REQUIRED_REQUEST:.*MiniShooterCore.*MiniTask19_AddWidgets.*SelectedGFD=1') { throw 'Ready did not include selected GameFeatureData HUD requests.' }
}
function Assert-RoleMedia($Entry, [string[]]$Stages, [string]$Text) {
    if (-not $WithMedia -or $Entry.Role -eq 'Dedicated') { return }
    foreach ($stage in $Stages) {
        $path = Join-Path $media "Task26Roles-$($Entry.Scenario)-$($Entry.Peer)-$stage.png"
        if (-not $Text.Contains("Stage=$stage Path=") -or -not (Test-Path -LiteralPath $path -PathType Leaf) -or
            (Get-Item -LiteralPath $path).Length -le 1024) { throw "Role rendered evidence missing: $path" }
    }
}
function Assert-RoleMatrix($Entries, [string]$Scenario) {
    $reasons = @()
    foreach ($entry in $Entries) {
        [string]$text = Read-RoleHealthy $entry
        Assert-RoleCount $text 'MiniTask26Role CLAIM:' 1 "$($entry.Peer) exact target World"
        Assert-RoleCount $text 'MiniTask26Role PASS:' 1 "$($entry.Peer) normal exit acceptance"
        if ($Scenario -eq 'Normal') {
            Assert-RoleReady $entry $text
            Assert-RoleCount $text 'MiniTask26Role FAILED_OBSERVED:|Experience state .* -> Failed' 0 "$($entry.Peer) no failure"
            if ($text -notmatch 'PASS:.*Claimed=1 Loaded=1 Failed=0 Mutations=0') { throw 'Normal role final counters incorrect.' }
            Assert-RoleMedia $entry @('Ready') $text
        } else {
            Assert-RoleCount $text 'MiniTask26Role FAILED_OBSERVED:' 1 "$($entry.Peer) actual OnExperienceFailed"
            Assert-RoleCount $text 'MiniTask26Role FAILURE_VERIFIED:' 1 "$($entry.Peer) native failure state"
            Assert-RoleCount $text 'Experience state .* -> Failed Reason=' 1 "$($entry.Peer) genuine terminal transition"
            $proof = [regex]::Match($text, 'MiniTask26Role FAILURE_VERIFIED:.*ReasonB64=([A-Za-z0-9+/=]+)')
            if (-not $proof.Success) { throw 'Failure reason comparison evidence absent.' }
            $reasons += $proof.Groups[1].Value
            $mutation = if ($entry.Role -eq 'Client') { '0' } else { '1' }
            if ($text -notmatch ("PASS:.*Claimed=1 Loaded=\d+ Failed=1 Mutations=$mutation")) { throw 'Authority-only mutation proof missing.' }
            if ($Scenario -eq 'ActorClassFailure') {
                if ($text -notmatch 'FAILURE_VERIFIED:.*Pawn=0 ActionResources=0') { throw 'Central failure allowed gameplay Pawn or Actor resources.' }
                if ($entry.Role -ne 'Client') {
                    Assert-RoleCount $text 'MiniTask26Role CENTRAL_INJECTED:.*Authority=1 Count=1 RealRequestAsyncLoad=1' 1 'authority central real async request'
                    if ($text -notmatch 'PASS:.*Loaded=0 Failed=1 Mutations=1') { throw 'Authority loaded before its required Actor class failure.' }
                } else { Assert-RoleCount $text 'MiniTask26Role CENTRAL_INJECTED:' 0 'client snapshot remained untouched' }
                Assert-RoleMedia $entry @('Failure') $text
            } else {
                Assert-RoleReady $entry $text
                if ($text -notmatch 'PASS:.*Loaded=1 Failed=1' -or $text.IndexOf('MiniTask26Role READY:') -ge $text.IndexOf('MiniTask26Role FAILED_OBSERVED:')) { throw 'Late failure did not prove real Loaded/Ready before Failed.' }
                Assert-RoleCount $text 'MiniTask26Role LATE_UI_REARMED:' $(if ($entry.Role -eq 'Listen') { 1 } else { 0 }) 'late real World self-load rearm'
                Assert-RoleMedia $entry @('Ready', 'Failure') $text
            }
            if ($entry.Role -ne 'Dedicated' -and $text -notmatch 'FAILURE_VERIFIED:.*NativeModal=1') { throw 'Failure did not show the native recovery Modal.' }
        }
    }
    if ($Scenario -ne 'Normal' -and ($reasons | Sort-Object -Unique).Count -ne 1) { throw 'The authority terminal reason was not received identically by both real clients.' }
}
function Run-RoleMatrix([string]$Scenario, [string]$Kind) {
    $finish = Join-Path $signals "$Scenario-$Kind-finish.signal"
    $inject = Join-Path $signals "$Scenario-$Kind-inject.signal"
    $entries = @()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    try {
        $entries += Start-RolePeer $Scenario $Kind "$Kind-Server" $finish $inject
        Wait-RoleMarkers $entries 'CLAIM:' $deadline
        if ($Kind -ne 'Standalone') {
            $entries += Start-RolePeer $Scenario 'Client' "$Kind-Client1" $finish $inject
            $entries += Start-RolePeer $Scenario 'Client' "$Kind-Client2" $finish $inject
        }
        if ($Scenario -in @('Normal', 'LateUIFailure')) {
            Wait-RoleMarkers $entries 'READY:' $deadline
            foreach ($entry in $entries) { Assert-RoleReady $entry (Read-RoleHealthy $entry) }
        }
        if ($Scenario -eq 'LateUIFailure') {
            # Only after all three real Worlds/Managers/UI are genuinely ready.
            Set-Content -LiteralPath $inject -Value 'AllThreeRealPeersReady' -Encoding ascii
        }
        if ($Scenario -ne 'Normal') { Wait-RoleMarkers $entries 'FAILURE_VERIFIED:' $deadline }
        # Evidence is complete; normal RequestExit is permitted in every peer.
        foreach ($entry in $entries) { $entry.Accepted = $true }
        Set-Content -LiteralPath $finish -Value 'AllPeerEvidenceObserved' -Encoding ascii
        $exitDeadline = [DateTime]::UtcNow.AddSeconds(30)
        do {
            $allExited = $true
            foreach ($entry in $entries) { $null = Read-RoleHealthy $entry; if (-not $entry.Process.HasExited) { $allExited = $false } }
            if ($allExited) { break }
            Start-Sleep -Milliseconds 250
        } while ([DateTime]::UtcNow -lt $exitDeadline)
        foreach ($entry in $entries) { if (-not $entry.Process.HasExited) { throw "Role normal exit timed out: $($entry.Peer)" } }
        Assert-RoleMatrix $entries $Scenario
        Write-Host "Task26 roles $Scenario $Kind PASS: real role scope/readiness, identical authority failure where requested, all Exit0."
    } finally {
        foreach ($entry in $entries) {
            $entry.Process.Refresh()
            if (-not $entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id -Force; $null = $entry.Process.WaitForExit(10000) }
        }
    }
}

$kinds = if ($ServerKind -eq 'Both') { @('Listen', 'Dedicated') } else { @($ServerKind) }
if ($Mode -in @('All', 'Normal')) {
    Run-RoleMatrix 'Normal' 'Standalone'
    foreach ($kind in $kinds) { Run-RoleMatrix 'Normal' $kind }
}
if ($Mode -in @('All', 'ActorClassFailure')) { foreach ($kind in $kinds) { Run-RoleMatrix 'ActorClassFailure' $kind } }
if ($Mode -in @('All', 'LateUIFailure')) {
    if ($ServerKind -eq 'Dedicated') { throw 'LateUIFailure requires a Listen authority with real local UI; use -ServerKind Listen or Both.' }
    Run-RoleMatrix 'LateUIFailure' 'Listen'
}
Write-Host 'Task26 role/authority-replication matrix passed. Late UI is genuine self-load after diagnostic rearm, not actor destruction or network lateness.'

[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$PackagedExe = '',
    [ValidateSet('All', 'Unknown', 'MissingFeature', 'MissingPrimary', 'FrontClass', 'HUDClass', 'ActorClass',
        'UIAsyncFailure', 'ActorAsyncFailure', 'NoLayer', 'NoSlot', 'AssetReturn', 'FeatureReturn',
        'ResourcesReturn', 'UIActionReturn', 'ActorActionReturn', 'Deadline', 'FailedFrontQuit',
        'StaleFailure', 'NormalPractice')][string]$Mode = 'All',
    [ValidateRange(90, 900)][int]$TimeoutSeconds = 240,
    [switch]$WithMedia,
    [string]$ProjectRoot = ''
)
$ErrorActionPreference = 'Stop'
# A staging draft can be parsed/read without starting UE. After installation the
# parent of Scripts is the project; callers may name ProjectRoot explicitly.
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$projectPath = [IO.Path]::GetFullPath($ProjectRoot)
$projectFile = Join-Path $projectPath 'FPS.uproject'
$runner = if ($PackagedExe) { [IO.Path]::GetFullPath($PackagedExe) } else { Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' }
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) { throw "Missing Development runner: $runner" }
if (-not $PackagedExe -and -not (Test-Path -LiteralPath $projectFile -PathType Leaf)) { throw "Missing project: $projectFile" }
$logs = Join-Path $projectPath 'Saved\Logs'
$cache = Join-Path $projectPath 'DerivedDataCache'
$media = Join-Path $projectPath 'Saved\Screenshots'
New-Item -ItemType Directory -Path $logs, $cache, $media -Force | Out-Null
$prefix = if ($WithMedia) { 'Task26-Faults-WithMedia' } else { 'Task26-Faults' }
$pendingModes = @('AssetReturn', 'FeatureReturn', 'ResourcesReturn', 'UIActionReturn', 'ActorActionReturn')
$selfModes = @('UIAsyncFailure', 'ActorAsyncFailure', 'NoLayer', 'NoSlot', 'StaleFailure')
$failureModes = @('Unknown', 'MissingFeature', 'MissingPrimary', 'FrontClass', 'HUDClass', 'ActorClass',
    'UIAsyncFailure', 'ActorAsyncFailure', 'NoLayer', 'NoSlot', 'Deadline', 'FailedFrontQuit', 'StaleFailure')
$classModes = @('FrontClass', 'HUDClass', 'ActorClass', 'FailedFrontQuit')

function Assert-Count([string]$Text, [string]$Pattern, [int]$Expected, [string]$Label) {
    $count = [regex]::Matches($Text, $Pattern).Count
    if ($count -ne $Expected) { throw "$Label expected $Expected records, got $count." }
}
function Read-Healthy($Entry) {
    $Entry.Process.Refresh()
    $text = if (Test-Path -LiteralPath $Entry.Log) { [string](Get-Content -LiteralPath $Entry.Log -Raw) } else { '' }
    if ($text -match 'MiniTask26Probe FAIL:|Fatal error:|Assertion failed:|Ensure condition failed:|Found duplicate PrimaryAssetID|CreateSavedMove: Hit limit') {
        throw "Unexpected probe/runtime failure: $($Entry.Log)"
    }
    if ($text -match 'BroadcastNetworkFailure:|BroadcastTravelFailure:') { throw "Fault fixture caused an unrelated engine travel/network failure: $($Entry.Log)" }
    if ($Entry.Process.HasExited -and $Entry.Process.ExitCode -ne 0) { throw "Runner exited with $($Entry.Process.ExitCode): $($Entry.Log)" }
    return $text
}
function Assert-ExpectedErrors([string]$Text, [string]$Scenario) {
    foreach ($errorLine in [regex]::Matches($Text, '(?m)^[^\r\n]*: Error:[^\r\n]*$')) {
        [string]$line = $errorLine.Value
        $allowed = $false
        if ($Scenario -in $failureModes -and $line -match 'LogMiniExperience: Error: Experience state .* -> Failed Reason=') {
            $allowed = switch ($Scenario) {
                'Unknown' { $line -match 'Unknown Experience ID.*DA_MiniTask26DefinitelyMissing' }
                'MissingFeature' { $line -match 'GameFeature plugin.*MiniDefinitelyMissing.*was not found' }
                'MissingPrimary' { $line -match 'DA_MiniTask26MissingPrimary.*finished loading without a MiniExperienceDefinition object' }
                'Deadline' { $line -match 'Experience loading exceeded 60 seconds' }
                'UIAsyncFailure' { $line -match 'Required Action.*required widget class or tag is invalid' }
                'StaleFailure' { $line -match 'Required Action.*required widget class or tag is invalid' }
                'ActorAsyncFailure' { $line -match 'Required Action.*class must be concrete and replicated' }
                'NoLayer' { $line -match 'Required Action.*NO_LAYER_OR_WIDGET' }
                'NoSlot' { $line -match 'Required Action.*REQUIRED_EXTENSION_NOT_ATTACHED' }
                default { $line -match ('Required Action.*Mini/Diagnostics/Task26/Missing' + [regex]::Escape($Scenario)) }
            }
        }
        if ($Scenario -in @('MissingPrimary', 'FrontClass', 'HUDClass', 'ActorClass', 'UIAsyncFailure', 'ActorAsyncFailure', 'FailedFrontQuit', 'StaleFailure') -and
            $line -match 'Log(Streaming|UObjectGlobals|Linker|PackageName): Error:' -and $line -match '/Game/Mini/Diagnostics/Task26/') { $allowed = $true }
        if ($Scenario -in @('UIAsyncFailure', 'StaleFailure') -and $line -match 'LogMiniInit: Error: MiniAddWidgets REQUIRED_FAILED:.*Reason=required widget class or tag is invalid') { $allowed = $true }
        if ($Scenario -eq 'NoLayer' -and $line -match 'LogMiniInit: Error: MiniAddWidgets REQUIRED_FAILED:.*Reason=NO_LAYER_OR_WIDGET') { $allowed = $true }
        if ($Scenario -eq 'NoSlot' -and $line -match 'LogMiniInit: Error: MiniAddWidgets REQUIRED_FAILED:.*Reason=REQUIRED_EXTENSION_NOT_ATTACHED') { $allowed = $true }
        if ($Scenario -eq 'ActorAsyncFailure' -and $line -match 'LogMiniInit: Error: MiniAddActors FAILED:.*Reason=class must be concrete and replicated') { $allowed = $true }
        if (-not $allowed) { throw "Unexpected Error in $Scenario (allow only the exact fixture cause): $line" }
    }
}
function Assert-Scenario($Entry, [string]$Text) {
    $scenario = $Entry.Scenario
    Assert-Count $Text 'MiniTask26Probe CLAIM:' 1 "$scenario claimed World"
    Assert-Count $Text 'MiniTask26Probe PASS:' 1 "$scenario final acceptance"
    if ($Text -notmatch ('MiniTask26Probe PASS: Mode=' + [regex]::Escape($scenario) + ' Claimed=1 Mutations=' + $(if ($scenario -eq 'NormalPractice') { '0' } else { '1' }) + ' ')) {
        throw "$scenario did not prove one fault mutation in one World."
    }
    if ($scenario -in $failureModes) {
        Assert-Count $Text 'MiniTask26Probe FAILED_OBSERVED:.*Target=1 Count=1 ' 1 "$scenario genuine terminal failure"
        Assert-Count $Text 'MiniTask26Probe FAILURE_VERIFIED:' 1 "$scenario native recovery"
        Assert-Count $Text 'Experience state .* -> Failed Reason=' 1 "$scenario manager terminal transition"
    } else {
        Assert-Count $Text 'MiniTask26Probe FAILED_OBSERVED:|Experience state .* -> Failed' 0 "$scenario no unexpected failure"
    }
    if ($scenario -in @('UIAsyncFailure', 'NoLayer', 'StaleFailure', 'UIActionReturn', 'NormalPractice')) {
        if ($Text -notmatch 'MiniTask26Probe PASS:.*TargetLoaded=1 ') { throw "$scenario did not observe genuine target Loaded exactly once." }
    } elseif ($scenario -ne 'NoSlot') {
        if ($Text -notmatch 'MiniTask26Probe PASS:.*TargetLoaded=0 ') { throw "$scenario target published Loaded before its required work was ready." }
    }
    if ($scenario -in $pendingModes) {
        Assert-Count $Text 'MiniTask26Probe PENDING_VERIFIED:' 1 "$scenario actual pending"
        Assert-Count $Text 'MiniTask26Probe RETURN_REQUEST:.*RealProductionAPI=1 PendingObserved=1' 1 "$scenario production return during pending"
    }
    if ($scenario -eq 'AssetReturn') {
        Assert-Count $Text 'MiniTask26Probe PRIMARY_STALLED_PATH:.*RealRequestAsyncLoad=1' 1 'registered primary-path real streamable hold'
    }
    if ($scenario -eq 'MissingPrimary') {
        Assert-Count $Text 'MiniTask26Probe PRIMARY_REGISTERED:.*Validated=1 ExistingProductionIdOverwritten=0' 1 'real PrimaryAsset registration'
        if ($Text -notmatch 'ID=MiniExperienceDefinition:DA_MiniTask26MissingPrimary Unloaded -> LoadingAssets') { throw 'MissingPrimary did not enter the actual PrimaryAsset load stage.' }
    }
    if ($scenario -in $classModes) { Assert-Count $Text 'MiniTask26Probe CLASS_PATH_INJECTED:.*Central=1' 1 "$scenario central missing class" }
    if ($scenario -in $selfModes) { Assert-Count $Text 'MiniTask26Probe WORLD_SNAPSHOT_INJECTED:.*CentralSnapshotChanged=0 AssetChanged=0' 1 "$scenario actual Action snapshot" }
    if ($scenario -in @('HUDClass', 'NoSlot', 'NormalPractice')) {
        if ($Text -notmatch 'MiniTask26Probe REQUIRED_REQUEST: Action=.*MiniShooterCore.*MiniTask19_AddWidgets.*Plugin=1') { throw "$scenario did not include the selected GameFeatureData HUD Action." }
    }
    if ($scenario -eq 'FeatureReturn') {
        Assert-Count $Text 'MiniTask26Probe FEATURE_PREPARED: Installed=1 InactiveFrontEnd=1' 1 'real feature unload preparation'
        Assert-Count $Text 'MiniTask26Probe FEATURE_REAL_PAUSED:.*State=Mounting ManagerPending=1' 1 'actual engine post-mount pause'
        Assert-Count $Text 'MiniFeature ActivationSucceeded .*MiniShooterCore' 0 'ended World must not accept late activation'
        Assert-Count $Text 'MiniFeature Release .*MiniShooterCore.*FinalUser=1 Activated=1' 1 'late activation real final lease release'
        if ($Text -notmatch 'MiniTask26Probe PASS:.*LateFeatureActivated=1 LateFeatureDeactivated=1') { throw 'Real feature late completion/deactivation was not observed.' }
    }
    if ($scenario -eq 'Deadline') {
        Assert-Count $Text 'MiniTask26Probe PENDING_VERIFIED:' 1 'bare startup actual required handle pending'
        if ($Text -notmatch 'LoadingActionResources -> Failed Reason=Experience loading exceeded 60 seconds') { throw 'Deadline was not the production required-resource deadline.' }
    }
    if ($scenario -eq 'StaleFailure') { Assert-Count $Text 'MiniTask26Probe STALE_FAILURE_REJECTED: ActualPayloadReplayed=1 OldWorldEnded=1 NewLoaded=1 NewFailed=0 NewError=0' 1 'captured stale failure rejection' }
    if ($scenario -eq 'FailedFrontQuit') {
        if ($Text -notmatch 'MiniTask26Probe PASS:.*WorldRecords=1 ') { throw 'Failed FrontEnd Quit created another World.' }
        if ($WithMedia) { Assert-Count $Text 'MiniTask26Probe UI_CLICK: Button=QuitButton.*Hover=1 Pressed=1 Capture=1 Down=1 Up=1' 1 'actual native Quit button click' }
        else { Assert-Count $Text 'MiniTask26Probe QUIT_API: NativeButtonPresent=1 ActualUIEvent=0' 1 'native button present and production Quit API' }
    } else {
        Assert-Count $Text 'MiniTask26Probe OLD_WORLD_RELEASED:' 1 "$scenario retained old World cleanup"
        Assert-Count $Text 'MiniTask26Probe RECOVERED: NewWorld=1 NewLoaded=1 NewFailed=0 Menu=1 Gate=1 InputSourceReleased=1 Consumed=1' 1 "$scenario unpolluted new World"
    }
    Assert-ExpectedErrors $Text $scenario
    if ($WithMedia) {
        $stages = if ($scenario -eq 'FailedFrontQuit') { @('ExpectedFailure') }
            elseif ($scenario -eq 'NormalPractice') { @('NormalPractice', 'RecoveredFrontEnd') }
            elseif ($scenario -in $failureModes) { @('ExpectedFailure', 'RecoveredFrontEnd') } else { @('RecoveredFrontEnd') }
        foreach ($stage in $stages) {
            $path = Join-Path $media "Task26-$scenario-$stage.png"
            if (-not $Text.Contains("Mode=$scenario Stage=$stage Path=") -or -not (Test-Path -LiteralPath $path -PathType Leaf) -or
                (Get-Item -LiteralPath $path).Length -le 1024) { throw "Actual rendered evidence missing: $path" }
        }
    }
}

$scenarios = if ($Mode -eq 'All') { @('NormalPractice', 'Unknown', 'MissingFeature', 'MissingPrimary', 'FrontClass', 'HUDClass', 'ActorClass',
    'UIAsyncFailure', 'ActorAsyncFailure', 'NoLayer', 'NoSlot', 'AssetReturn', 'FeatureReturn', 'ResourcesReturn', 'UIActionReturn',
    'ActorActionReturn', 'Deadline', 'FailedFrontQuit', 'StaleFailure') } else { @($Mode) }
foreach ($scenario in $scenarios) {
    $log = Join-Path $logs "$prefix-$scenario.log"
    if (Test-Path -LiteralPath $log -PathType Leaf) { Remove-Item -LiteralPath $log }
    $arguments = @()
    if (-not $PackagedExe) { $arguments += ('"{0}"' -f $projectFile) }
    $arguments += '/Game/Mini/Maps/L_MiniFrontEnd'
    if (-not $PackagedExe) { $arguments += '-game' }
    $arguments += @("-MiniProbeTask26=$scenario", "-MiniTask26TimeoutSeconds=$TimeoutSeconds", '-unattended', '-nosplash', '-nop4',
        '-ddc=InstalledNoZenLocalFallback', ('"-LocalDataCachePath={0}"' -f $cache), ('"-abslog={0}"' -f $log))
    if ($WithMedia) {
        $arguments += @('-MiniProbeTask26Media', ('"-MiniTask26MediaOutput={0}"' -f $media), '-windowed', '-ResX=960', '-ResY=540',
            '"-ExecCmds=t.MaxFPS 30,sg.GlobalIlluminationQuality 0,sg.ReflectionQuality 0,sg.ShadowQuality 1,r.MotionBlurQuality 0"')
    } else { $arguments += @('-nullrhi', '-nosound') }
    $process = Start-Process -FilePath $runner -ArgumentList $arguments -WorkingDirectory $projectPath -WindowStyle Hidden -PassThru
    $entry = @{ Process=$process; Log=$log; Scenario=$scenario }
    Write-Host "Task26 fault $scenario started. PID=$($process.Id) Log=$log"
    try {
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 30)
        do {
            [string]$text = Read-Healthy $entry
            if ($process.HasExited) { break }
            Start-Sleep -Milliseconds 250
        } while ([DateTime]::UtcNow -lt $deadline)
        $process.Refresh()
        if (-not $process.HasExited) { throw "Task26 $scenario did not terminate normally before its finite deadline: $log" }
        [string]$text = Read-Healthy $entry
        Assert-Scenario $entry $text
        Write-Host "Task26 $scenario PASS: genuine cause/pending path, one World, retained cleanup, recovery, normal Exit0."
    } finally {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force; $null = $process.WaitForExit(10000) }
    }
}
Write-Host 'Task26 fault matrix passed. Weak-network and real client RPC acceptance remain a separate required run.'

[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateRange(60, 1800)][int]$TimeoutSeconds = 300,
    [ValidateRange(1024, 65535)][int]$Port = 18816
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
$cacheDirectory = Join-Path $projectRoot 'DerivedDataCache'
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) {
    throw "UnrealEditor-Cmd.exe was not found under $EngineRoot"
}
New-Item -ItemType Directory -Path $logDirectory, $cacheDirectory -Force | Out-Null

function Read-ProbeLog([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        return [string](Get-Content -LiteralPath $Path -Raw)
    }
    return [string]::Empty
}

function Start-Probe([string]$Url, [string]$Role, [string]$LogPath, [string[]]$ExtraArguments) {
    if (Test-Path -LiteralPath $LogPath -PathType Leaf) {
        Remove-Item -LiteralPath $LogPath
    }
    $arguments = @(
        ('"{0}"' -f $projectFile), $Url, '-game', '-MiniProbeTask16',
        '-unattended', '-nosplash', '-nullrhi', '-nosound', '-nop4',
        '-ddc=InstalledNoZenLocalFallback',
        ('"-LocalDataCachePath={0}"' -f $cacheDirectory),
        ('"-abslog={0}"' -f $LogPath)
    ) + $ExtraArguments
    $process = Start-Process -FilePath $editor -ArgumentList $arguments `
        -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru
    Write-Host "Task 16 $Role started: PID=$($process.Id), log=$LogPath"
    return $process
}

function Assert-Healthy([System.Diagnostics.Process]$Process, [string]$Role, [string]$LogPath) {
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "$Role exited early with code $($Process.ExitCode). See $LogPath"
    }
    $logText = Read-ProbeLog $LogPath
    if ($logText -match 'MiniTask16Probe FAIL:' -or
        $logText -match 'Experience state .* -> Failed') {
        throw "$Role reported a Task 16 or Experience failure. See $LogPath"
    }
    return $logText
}

$serverLog = Join-Path $logDirectory 'Task16-Server.log'
$clientALog = Join-Path $logDirectory 'Task16-ClientA.log'
$clientBLog = Join-Path $logDirectory 'Task16-ClientB.log'
$server = $null
$clientA = $null
$clientB = $null
try {
    $server = Start-Probe '/Game/Mini/Maps/L_MiniPractice?listen' 'listen server' $serverLog @("-port=$Port")
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $listenerPattern = '(?im)\bLogNet:.*\blistening on port\s+' + $Port + '\b'
    while ([DateTime]::UtcNow -lt $deadline) {
        $serverText = Assert-Healthy $server 'server' $serverLog
        if ($serverText -match $listenerPattern) { break }
        Start-Sleep -Milliseconds 500
    }
    if ($serverText -notmatch $listenerPattern) {
        throw "Server did not start listening. See $serverLog"
    }

    $clientA = Start-Probe "127.0.0.1:$Port" 'ClientA' $clientALog @()
    $clientB = Start-Probe "127.0.0.1:$Port" 'ClientB' $clientBLog @()
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $serverText = Assert-Healthy $server 'server' $serverLog
        $clientAText = Assert-Healthy $clientA 'ClientA' $clientALog
        $clientBText = Assert-Healthy $clientB 'ClientB' $clientBLog
        $combined = $clientAText + "`n" + $clientBText
        if ($serverText -match 'MiniTask16Probe SERVER_PASS: InputGA=1 DamageGE=1 Replay=1 BadView=1 Rate=1 Empty=1 Wall=1 Muzzle=1 Death=1' -and
            ([regex]::Matches($combined, 'MiniTask16Probe CLIENT_READY: Role=Shooter')).Count -eq 1 -and
            ([regex]::Matches($combined, 'MiniTask16Probe CLIENT_READY: Role=Target')).Count -eq 1 -and
            ([regex]::Matches($combined, 'MiniTask16Probe CLIENT_FIRE_PRESSED: Input=LeftMouseButton')).Count -eq 1 -and
            ([regex]::Matches($combined, 'MiniTask16Probe CLIENT_FIRE_RELEASED: Input=LeftMouseButton')).Count -eq 1 -and
            ([regex]::Matches($combined, 'MiniTask16Probe CLIENT_DEATH: Role=Shooter Health=0 Dead=1')).Count -eq 1 -and
            ([regex]::Matches($combined, 'MiniTask16Probe CLIENT_DEATH: Role=Target Health=0 Dead=1')).Count -eq 1) {
            Write-Host 'Task 16 PASS: client input/GA, server hit/damage/death, both client observations, and authority rejections.'
            return
        }
        Start-Sleep -Milliseconds 500
    }
    throw "Task 16 timed out. See $serverLog, $clientALog, $clientBLog"
}
finally {
    foreach ($process in @($server, $clientA, $clientB)) {
        if ($null -ne $process) {
            $process.Refresh()
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
            }
        }
    }
}

[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [ValidateSet('FPSEditor', 'FPS')]
    [string]$Target = 'FPSEditor',
    [ValidateSet('Development', 'DebugGame', 'Shipping')]
    [string]$Configuration = 'Development',
    [switch]$DisableAdaptiveUnity
)

$ErrorActionPreference = 'Stop'
if ($Target -eq 'FPSEditor' -and $Configuration -eq 'Shipping') {
    throw 'Editor targets do not support Shipping. Use FPS or Development.'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'FPS.uproject'
$buildScript = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path -LiteralPath $buildScript)) {
    throw "Build.bat was not found. Supply the installed engine using -EngineRoot: $EngineRoot"
}
$logDirectory = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$buildLog = Join-Path $logDirectory "Build-$Target-Win64-$Configuration.log"
$ubaRoot = Join-Path $projectRoot 'Saved\BuildCache\UBA'
New-Item -ItemType Directory -Path $ubaRoot -Force | Out-Null

# UE 5.8 still uses the UBA executor with -NoUBA (detouring is disabled).
# Keep its storage in this project rather than a system-wide ProgramData folder.
$unityArguments = @()
if ($DisableAdaptiveUnity) { $unityArguments += '-DisableAdaptiveUnity' }
& $buildScript $Target 'Win64' $Configuration "-Project=$projectFile" `
    '-WaitMutex' '-NoHotReloadFromIDE' '-NoUBA' '-NoXGE' "-UBARootDir=$ubaRoot" "-Log=$buildLog" @unityArguments
if ($LASTEXITCODE -ne 0) {
    throw "UnrealBuildTool failed with exit code $LASTEXITCODE. See $buildLog"
}
Write-Host "Build succeeded: $Target Win64 $Configuration"
Write-Host "Log: $buildLog"

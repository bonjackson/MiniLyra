[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path -LiteralPath $uat -PathType Leaf)) { throw "RunUAT unavailable: $uat" }
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $env:TEMP ('MiniLyraTask20-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (Test-Path -LiteralPath $OutputRoot) { throw "Use a fresh output directory: $OutputRoot" }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$logs = Join-Path $projectRoot 'Saved\Logs'
New-Item -ItemType Directory -Path $logs -Force | Out-Null
$log = Join-Path $logs 'Task20-PackageConsole.log'
$cook = Join-Path $OutputRoot 'Cook\Windows'
$stage = Join-Path $OutputRoot 'Stage'

# Build FPS Development before this script. A new cook avoids inheriting the
# task 06 package, which predates the character, weapons and CommonGame HUD.
# Pak bypasses this machine's unresolved IoStore/Zen staging dependency.
& $uat BuildCookRun "-project=$(Join-Path $projectRoot 'FPS.uproject')" '-noP4' '-platform=Win64' `
    '-clientconfig=Development' '-skipbuild' '-cook' '-stage' '-package' '-pak' '-skipiostore' `
    '-AdditionalCookerOptions=-SkipZenStore' "-cookoutputdir=$cook" "-stagingdirectory=$stage" `
    '-map=/Game/Mini/Maps/L_MiniPractice' '-unattended' *> $log
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $log -SimpleMatch 'BUILD SUCCESSFUL' -Quiet)) {
    throw "Task 20 packaging failed. See $log"
}
$exe = Join-Path $stage 'Windows\FPS\Binaries\Win64\FPS.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Package executable missing: $exe" }
$head = (& git -C $projectRoot rev-parse HEAD).Trim()
$manifest = [ordered]@{
    CreatedUtc = [DateTime]::UtcNow.ToString('o')
    BaseCommit = $head
    Configuration = 'Development'
    Map = '/Game/Mini/Maps/L_MiniPractice'
    Format = 'Pak (skip IoStore / Zen Store)'
    CookDirectory = $cook
    StageDirectory = $stage
    Executable = $exe
    ConsoleLog = $log
    RuntimeVerified = $false
}
$manifestPath = Join-Path $projectRoot 'Saved\Task20Package.json'
$manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Host "Task 20 package created: $exe"
Write-Host "Manifest: $manifestPath"
Write-Host "Run VerifyTask20.ps1 -PackagedExe '$exe' (and -WithMedia) before recording a runtime PASS."

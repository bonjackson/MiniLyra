[CmdletBinding()]
param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$lyraContent = 'F:\LyraStarterGame\Content'
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) { throw "Missing editor: $editor" }
if (-not (Test-Path -LiteralPath $lyraContent -PathType Container)) { throw "Missing Lyra content: $lyraContent" }

$runRoot = Join-Path $projectRoot ('Saved\Task18WeaponReloadMigration\' + [guid]::NewGuid().ToString('N'))
$isolatedContent = Join-Path $runRoot 'Content'
$logPath = Join-Path $runRoot 'Migration.log'
$projectPath = Join-Path $runRoot 'Task18WeaponReloadMigration.uproject'
New-Item -ItemType Directory -Path $isolatedContent -Force | Out-Null
Set-Content -LiteralPath $projectPath -Value '{"FileVersion":3,"EngineAssociation":"5.8","Category":"","Description":"Isolated Task 18 weapon reload migration"}' -Encoding utf8

$relativeSources = @(
    'Rifle\Mesh\SK_Rifle_Skeleton.uasset',
    'Rifle\Animations\Weap_Rifle_Reload.uasset',
    'Pistol\Mesh\SK_Pistol_Skeleton.uasset',
    'Pistol\Animations\Weap_Pistol_Reload.uasset'
)
foreach ($relative in $relativeSources) {
    $source = Join-Path (Join-Path $lyraContent 'Weapons') $relative
    $destination = Join-Path (Join-Path $isolatedContent 'Weapons') $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing Lyra animation asset: $source" }
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}

$cacheDirectory = Join-Path $runRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null
& $editor $projectPath '-run=pythonscript' "-script=$(Join-Path $PSScriptRoot 'Task18MigrateWeaponReload.py')" `
    '-EnablePlugins=PythonScriptPlugin' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' `
    '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" "-abslog=$logPath" | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -SimpleMatch 'MINI_TASK18_RELOAD_MIGRATED Count=2' -Quiet)) {
    throw "Isolated weapon reload migration failed. See $logPath"
}

$relativeOutputs = @(
    'Rifle\Animations\Weap_Rifle_Reload.uasset',
    'Pistol\Animations\Weap_Pistol_Reload.uasset'
)
foreach ($relative in $relativeOutputs) {
    $source = Join-Path (Join-Path $isolatedContent 'Mini\Weapons') $relative
    $destination = Join-Path (Join-Path $projectRoot 'Content\Mini\Weapons') $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Migration did not save: $source" }
    if (Test-Path -LiteralPath $destination) { throw "Refusing to overwrite existing reload sequence: $destination" }
}
foreach ($relative in $relativeOutputs) {
    $source = Join-Path (Join-Path $isolatedContent 'Mini\Weapons') $relative
    $destination = Join-Path (Join-Path $projectRoot 'Content\Mini\Weapons') $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash) {
        throw "Reload sequence copy hash mismatch: $relative"
    }
}
Write-Host "Task 18 weapon reload sequences migrated into Mini. Isolated log: $logPath"

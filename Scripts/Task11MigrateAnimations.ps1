[CmdletBinding()]
param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$templateContent = Join-Path $EngineRoot 'Templates\TemplateResources\High\Characters\Content'
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) { throw "Missing editor: $editor" }
if (-not (Test-Path -LiteralPath $templateContent -PathType Container)) { throw "Missing template content: $templateContent" }

$runRoot = Join-Path $projectRoot ('Saved\Task11AnimationMigration\' + [guid]::NewGuid().ToString('N'))
$isolatedContent = Join-Path $runRoot 'Content'
$logPath = Join-Path $runRoot 'Migration.log'
$projectPath = Join-Path $runRoot 'Task11AnimationMigration.uproject'
New-Item -ItemType Directory -Path $isolatedContent -Force | Out-Null
Set-Content -LiteralPath $projectPath -Value '{"FileVersion":3,"EngineAssociation":"5.8","Category":"","Description":"Isolated Task 11 animation migration"}' -Encoding utf8

$relativeSources = @(
    'Mannequins\Meshes\SK_Mannequin.uasset',
    'Mannequins\Meshes\SKM_Quinn_Simple.uasset',
    'Mannequins\Anims\Unarmed\Jump\MM_Jump.uasset',
    'Mannequins\Anims\Unarmed\Jump\MM_Fall_Loop.uasset',
    'Mannequins\Anims\Unarmed\Jump\MM_Land.uasset',
    'Mannequins\Anims\Rifle\MF_Rifle_Idle_ADS.uasset',
    'Mannequins\Anims\Rifle\Jog\MF_Rifle_Jog_Fwd.uasset'
)
foreach ($relative in $relativeSources) {
    $source = Join-Path $templateContent $relative
    $destination = Join-Path (Join-Path $isolatedContent 'Characters') $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing source animation: $source" }
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}

$cacheDirectory = Join-Path $runRoot 'DerivedDataCache'
New-Item -ItemType Directory -Path $cacheDirectory -Force | Out-Null
& $editor $projectPath '-run=pythonscript' "-script=$(Join-Path $PSScriptRoot 'Task11MigrateAnimations.py')" `
    '-EnablePlugins=PythonScriptPlugin' '-unattended' '-nosplash' '-nullrhi' '-nosound' '-nop4' `
    '-ddc=InstalledNoZenLocalFallback' "-LocalDataCachePath=$cacheDirectory" "-abslog=$logPath" | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $logPath -SimpleMatch 'MINI_TASK11_ANIMATIONS_MIGRATED' -Quiet)) {
    throw "Isolated animation migration failed. See $logPath"
}

$relativeOutputs = $relativeSources | Where-Object { $_ -notmatch '^Mannequins\\Meshes\\' }
foreach ($relative in $relativeOutputs) {
    $source = Join-Path (Join-Path $isolatedContent 'Mini\Characters') $relative
    $destination = Join-Path (Join-Path $projectRoot 'Content\Mini\Characters') $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Migration did not save: $source" }
    if (Test-Path -LiteralPath $destination) { throw "Refusing to overwrite existing animation: $destination" }
}
foreach ($relative in $relativeOutputs) {
    $source = Join-Path (Join-Path $isolatedContent 'Mini\Characters') $relative
    $destination = Join-Path (Join-Path $projectRoot 'Content\Mini\Characters') $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}
Write-Host "Task 11 animation assets migrated from UE 5.8 template into Mini. Isolated log: $logPath"

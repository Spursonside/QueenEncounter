param(
    [string]$EngineRoot='D:\Epic Games\UE_5.8',
    [ValidateSet('manual','forward','left','right','back','rotate','arc','change')][string]$Scenario='manual',
    [ValidateRange(0,63)][int]$DamageMask=0,
    [switch]$Hills,
    [switch]$Player,
    [switch]$Combat
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$run=Join-Path $projectRoot ('LearningRuns\adaptive_view_'+(Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Path $run | Out-Null
$options=@("-QueenDamageMask=$DamageMask")
if($Scenario -eq 'manual' -and !$Player){$options+='-QueenManual'}
else {if($Scenario -eq 'manual'){$Scenario='forward'}; $options+="-QueenScenario=$Scenario"}
if($Hills){$options+=@('-QueenStartX=6000','-QueenStartY=2000')}
if($Player){$options+='-QueenPlayerTest'}
if($Player -and !$Combat){$options+='-QueenSandbox'}
# Manual queen controls: W/S forward/back, A/D strafe, Q/E turn, F4 one leg, F5 front four, R reset.
# Player mode: existing WASD/mouse/fire controls; F4/F5 also damage the queen.
# Automatic runs repeat after completion. No video capture.
& (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') (Join-Path $projectRoot 'QueenEncounter.uproject') '/Game/Maps/ArthropodTerrain?game=/Script/QueenEncounter.QueenTraversalMode' -game -windowed -ResX=1400 -ResY=900 -nosplash -nosound -QueenDemoLoop '-ExecCmds=t.MaxFPS 60' "-QueenResult=$run\result.json" "-abslog=$run\viewer.log" @options

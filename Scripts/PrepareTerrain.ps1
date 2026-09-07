param([string]$EngineRoot='D:/Epic Games/UE_5.8')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$editor=Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
foreach($script in @('create_queen_armor.py','create_terrain.py')) {
    & $editor (Join-Path $root 'QueenEncounter.uproject') -run=pythonscript "-script=$PSScriptRoot/$script" -QueenArthropodTerrain -unattended -nullrhi -nosplash
    if($LASTEXITCODE -ne 0){throw "Asset preparation failed: $script"}
}

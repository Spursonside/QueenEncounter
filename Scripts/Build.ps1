param([string]$EngineRoot = 'D:\Epic Games\UE_5.8', [switch]$Package)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path $PSScriptRoot -Parent
$ProjectFile = Join-Path $ProjectRoot 'QueenEncounter.uproject'
$BuildTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
if (!(Test-Path -LiteralPath $BuildTool)) { throw "Engine build tool not found: $BuildTool" }
& $BuildTool QueenEncounterEditor Win64 Development "-Project=$ProjectFile" -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw 'Editor build failed. Close any running project editor/game before building.' }
$EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
& $EditorCmd $ProjectFile -run=pythonscript "-script=$PSScriptRoot\create_assets.py" -unattended -nullrhi -nosplash
if ($LASTEXITCODE -ne 0) { throw 'Asset generation failed.' }
if ($Package) {
    $UAT = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
    & $UAT BuildCookRun "-project=$ProjectFile" -noP4 -platform=Win64 -clientconfig=Development -build -cook -map=/Game/Maps/Arena -stage -pak -archive "-archivedirectory=$ProjectRoot\BuildOutput" -unattended -utf8output
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
}

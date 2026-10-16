param([string]$Engine = 'D:/DKU_Academic/UE_5.7')
$projectFile = Join-Path (Split-Path $PSScriptRoot -Parent) 'RoomShiftDemo.uproject'
& "$Engine/Engine/Binaries/Win64/UnrealEditor.exe" $projectFile '/Game/RoomShift/RoomShift' '-game' '-windowed' '-ResX=1440' '-ResY=900' '-ForceRes' '-nosplash'

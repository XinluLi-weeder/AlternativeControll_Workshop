@echo off
setlocal
set "ROOMSHIFT_ENGINE_EXE="
if defined UE_ROOT if exist "%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe" set "ROOMSHIFT_ENGINE_EXE=%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe"
if not defined ROOMSHIFT_ENGINE_EXE if exist "D:\DKU_Academic\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" set "ROOMSHIFT_ENGINE_EXE=D:\DKU_Academic\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
if not defined ROOMSHIFT_ENGINE_EXE (
  echo Unreal Engine 5.7 was not found.
  echo Set UE_ROOT to your Unreal Engine 5.7 installation folder, then run this file again.
  echo Example: set UE_ROOT=C:\Program Files\Epic Games\UE_5.7
  echo Or open RoomShiftDemo.uproject in Unreal Editor and press Play.
  pause
  exit /b 1
)
start "Room Shift" "%ROOMSHIFT_ENGINE_EXE%" "%~dp0RoomShiftDemo.uproject" /Game/RoomShift/RoomShift -game -windowed -ResX=1440 -ResY=900 -ForceRes -nosplash

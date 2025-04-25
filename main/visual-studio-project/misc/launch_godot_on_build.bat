@echo off
echo Checking if Godot is running...
tasklist | findstr /I "Godot" >nul 2>&1
if errorlevel 1 (
    echo Godot is not running. Launching project in game mode...
    start "" "%~dp0..\..\..\Test-Project-BlastBullets2D\project.godot"
) else (
    echo Godot is already running. No action taken.
)
@echo off
rem Bust A Groove 2 recompilado — nativo (OpenGL). F11 = tela cheia. Teclado: setas, X=cruz, S=circulo,
rem Z=quadrado, A=triangulo, Enter=Start, Shift direito=Select (keybinds.ini). Gamepad: automatico.
rem Funciona tanto na pasta do exe (releases\..., build\) quanto na pasta-fonte (usa build\).
setlocal
set DIR=%~dp0
if not exist "%DIR%Bust_A_Groove_2_Recompiled.exe" set DIR=%~dp0build\
if not exist "%DIR%Bust_A_Groove_2_Recompiled.exe" (
    echo Exe nao encontrado em %~dp0 nem em %~dp0build\ — rode tools\recomp\build.ps1 -Step runtime
    pause
    exit /b 1
)
set DISC=%~dp0..\..\..\disc\Bust A Groove 2 (USA).cue
if not "%~1"=="" set DISC=%~1
cd /d "%DIR%"
start "" "%DIR%Bust_A_Groove_2_Recompiled.exe" --game game.toml --disc "%DISC%"

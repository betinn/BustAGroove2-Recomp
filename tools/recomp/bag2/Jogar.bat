@echo off
rem Bust A Groove 2 recompilado — nativo (OpenGL). F11 = tela cheia. Teclado: setas, X=cruz, S=circulo,
rem Z=quadrado, A=triangulo, Enter=Start, Shift direito=Select (keybinds.ini). Gamepad: automatico.
setlocal
cd /d "%~dp0"
set DISC=C:\Users\sem-m\OneDrive\Documentos\My Games\Bust a groove 2\Bust A Groove 2 (USA).cue
if not "%~1"=="" set DISC=%~1
start "" "Bust_A_Groove_2_Recompiled.exe" --game game.toml --disc "%DISC%"

@echo off
rem Sillage portable launcher: starts the engine, then opens the UI.
cd /d "%~dp0"
echo Demarrage de Sillage... (fermez cette fenetre pour arreter le moteur)
start "" /b cmd /c "timeout /t 2 >nul & start http://127.0.0.1:8080"
sillage-engine.exe %*

@echo off
setlocal
set "TRL_GAME_DIR=C:\Users\skurtyy\Documents\GitHub\AlmightyBackups\NightRaven1\Vibe-Reverse-Engineering-Claude\Tomb Raider Legend"
cd /d C:\Users\skurtyy\Documents\GitHub\TombRaiderLegendRTX-
echo TRL_GAME_DIR=%TRL_GAME_DIR%
"C:\Users\skurtyy\AppData\Local\Programs\Python\Python310\python.exe" patches\TombRaiderLegend\run.py test-hash --build --main-menu
exit /b %ERRORLEVEL%

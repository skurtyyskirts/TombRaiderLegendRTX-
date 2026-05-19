@echo off
setlocal
set "PATH=C:\Program Files\Git\cmd;%PATH%"
cd /d C:\Users\skurtyy\Documents\GitHub\TombRaiderLegendRTX-
git add "TRL tests/build-084-iter3-drop-bvi-mi-FAIL-cache-not-reached/"
git add "TRL tests/build-085-miracle-iter4-nv-cap-65535-main-menu-hash-stable/"
git add patches/TombRaiderLegend/proxy/d3d9_device.c
git add proxy/d3d9_device.c
git add patches/TombRaiderLegend/backups/2026-05-19_1029_iter3-drop-bvi-mi-from-key/
git add patches/TombRaiderLegend/backups/2026-05-19_1035_iter4-nv-cap-65535/
git add patches/TombRaiderLegend/git_do_commit.bat
echo === STAGED ===
git diff --cached --stat
echo === COMMIT ===
git -c user.email=jeffreyalanmunoz@gmail.com -c user.name=skurtyy commit -F C:\Users\skurtyy\Documents\GitHub\TombRaiderLegendRTX-\patches\TombRaiderLegend\.commit_msg.txt
echo === PUSH ===
git push origin main 2>&1
exit /b %ERRORLEVEL%

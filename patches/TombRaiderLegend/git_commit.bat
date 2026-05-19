@echo off
setlocal
set "PATH=C:\Program Files\Git\cmd;%PATH%"
cd /d C:\Users\skurtyy\Documents\GitHub\TombRaiderLegendRTX-
echo === STATUS ===
git status --short
echo === ADD ===
git add "TRL tests/build-082-bind-pose-cache-FAIL-gate-no-match-main-menu/"
git add "TRL tests/build-083-variant7-widen-gate-FAIL-cache-misses/"
git add patches/TombRaiderLegend/run.py
git add patches/TombRaiderLegend/run_iter_wrapper.py
git add patches/TombRaiderLegend/run_iter.bat
git add patches/TombRaiderLegend/proxy/d3d9_device.c
git add patches/TombRaiderLegend/proxy/proxy.ini
git add proxy/d3d9_device.c
git add patches/TombRaiderLegend/backups/2026-05-19_0956_sync-root-to-patches-build081/
git add patches/TombRaiderLegend/backups/2026-05-19_1016_variant7-widen-gate/
git add patches/TombRaiderLegend/git_commit.bat
git add patches/TombRaiderLegend/envtest.py
echo === STAGED ===
git diff --cached --stat
exit /b 0

@echo off
cd /d "%~dp0"
call build.bat > build_log.txt 2>&1
type build_log.txt

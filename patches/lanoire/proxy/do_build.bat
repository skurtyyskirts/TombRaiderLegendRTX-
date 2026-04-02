@echo off
set "MSVC_BIN=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x86"
set "MSVC_INC=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\include"
set "MSVC_LIB=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\lib\x86"
set "SDK_INC=C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0"
set "SDK_LIB=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0"
set "PATH=%MSVC_BIN%;%PATH%"
set "INCLUDE=%MSVC_INC%;%SDK_INC%\um;%SDK_INC%\shared;%SDK_INC%\ucrt"
set "LIB=%MSVC_LIB%;%SDK_LIB%\um\x86;%SDK_LIB%\ucrt\x86"

set "PROXY_DIR=%~dp0"
set "GAME_DIR=C:\Users\skurtyy\Documents\GitHub\AlmightyBackups\LA Noire\L.A.Noire"

cd /d "%PROXY_DIR%"

echo --- Compiling d3d9_main.c ---
cl.exe /nologo /O1 /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_main.c
if errorlevel 1 (echo COMPILE FAIL d3d9_main.c && exit /b 1)

echo --- Compiling d3d9_wrapper.c ---
cl.exe /nologo /O1 /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_wrapper.c
if errorlevel 1 (echo COMPILE FAIL d3d9_wrapper.c && exit /b 1)

echo --- Compiling d3d9_device.c ---
cl.exe /nologo /O1 /Oi /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_device.c
if errorlevel 1 (echo COMPILE FAIL d3d9_device.c && exit /b 1)

echo --- Linking d3d9.dll ---
link.exe /nologo /DLL /NODEFAULTLIB /ENTRY:_DllMainCRTStartup@12 /DEF:d3d9.def /OUT:d3d9.dll d3d9_main.obj d3d9_wrapper.obj d3d9_device.obj kernel32.lib
if errorlevel 1 (echo LINK FAIL && exit /b 1)

echo === BUILD OK ===
del *.obj *.lib *.exp 2>nul

echo Deploying to %GAME_DIR%...
copy /y d3d9.dll "%GAME_DIR%\d3d9.dll"
copy /y proxy.ini "%GAME_DIR%\proxy.ini"
echo === DEPLOYED ===

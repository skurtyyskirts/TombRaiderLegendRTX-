@echo off
call "C:\Program Files\Microsoft Visual Studio8\Insiders\VC\Auxiliary\Buildcvarsall.bat" x86
if errorlevel 1 (echo VCVARS FAILED & exit /b 1)
cd /d "%~dp0"
cl.exe /nologo /O1 /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_main.c
if errorlevel 1 (echo COMPILE FAIL & exit /b 1)
cl.exe /nologo /O1 /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_wrapper.c
if errorlevel 1 (echo COMPILE FAIL & exit /b 1)
cl.exe /nologo /O1 /Oi /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_device.c
if errorlevel 1 (echo COMPILE FAIL & exit /b 1)
link.exe /nologo /DLL /NODEFAULTLIB /ENTRY:_DllMainCRTStartup@12 /DEF:d3d9.def /OUT:d3d9.dll d3d9_main.obj d3d9_wrapper.obj d3d9_device.obj kernel32.lib
if errorlevel 1 (echo LINK FAIL & exit /b 1)
echo BUILD_OK
del *.obj *.lib *.exp 2>nul

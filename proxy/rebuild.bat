@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
echo Compiling d3d9_main.c...
cl.exe /nologo /O2 /Oi /fp:fast /GL /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_main.c
if errorlevel 1 goto :error
echo Compiling d3d9_wrapper.c...
cl.exe /nologo /O2 /Oi /fp:fast /GL /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_wrapper.c
if errorlevel 1 goto :error
echo Compiling d3d9_device.c...
cl.exe /nologo /O2 /Oi /fp:fast /GL /GS- /W3 /Zl /c /D "WIN32" /D "NDEBUG" d3d9_device.c
if errorlevel 1 goto :error
echo Linking d3d9.dll...
link.exe /nologo /DLL /NODEFAULTLIB /LTCG /ENTRY:_DllMainCRTStartup@12 /DEF:d3d9.def /OUT:d3d9.dll d3d9_main.obj d3d9_wrapper.obj d3d9_device.obj kernel32.lib
if errorlevel 1 goto :error
del *.obj *.lib *.exp 2>nul
echo === BUILD SUCCESS ===
exit /b 0
:error
echo === BUILD FAILED ===
exit /b 1

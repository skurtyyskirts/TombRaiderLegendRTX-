@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>nul

cd /d "%~dp0"

cl.exe /nologo /O2 /W3 /c /D "WIN32" /D "NDEBUG" d3d9_trace_main.c
if errorlevel 1 exit /b 1

cl.exe /nologo /O2 /W3 /c /D "WIN32" /D "NDEBUG" d3d9_trace_wrapper.c
if errorlevel 1 exit /b 1

cl.exe /nologo /O2 /W3 /c /D "WIN32" /D "NDEBUG" d3d9_trace_device.c
if errorlevel 1 exit /b 1

link.exe /nologo /DLL /DEF:d3d9.def /OUT:d3d9.dll d3d9_trace_main.obj d3d9_trace_wrapper.obj d3d9_trace_device.obj kernel32.lib user32.lib
if errorlevel 1 exit /b 1

copy /Y d3d9.dll ..\bin\d3d9.dll >nul
del *.obj *.lib *.exp d3d9.dll 2>nul
echo BUILD_OK

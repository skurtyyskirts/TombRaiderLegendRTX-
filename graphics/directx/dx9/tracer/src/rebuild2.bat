@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1

cl.exe /nologo /O2 /W3 /c /D "WIN32" /D "NDEBUG" d3d9_trace_main.c >C:\TEMP\build_log.txt 2>&1
if errorlevel 1 (echo COMPILE_FAIL_MAIN >>C:\TEMP\build_log.txt & exit /b 1)

cl.exe /nologo /O2 /W3 /c /D "WIN32" /D "NDEBUG" d3d9_trace_wrapper.c >>C:\TEMP\build_log.txt 2>&1
if errorlevel 1 (echo COMPILE_FAIL_WRAPPER >>C:\TEMP\build_log.txt & exit /b 1)

cl.exe /nologo /O2 /W3 /c /D "WIN32" /D "NDEBUG" d3d9_trace_device.c >>C:\TEMP\build_log.txt 2>&1
if errorlevel 1 (echo COMPILE_FAIL_DEVICE >>C:\TEMP\build_log.txt & exit /b 1)

link.exe /nologo /DLL /DEF:d3d9.def /OUT:d3d9.dll d3d9_trace_main.obj d3d9_trace_wrapper.obj d3d9_trace_device.obj kernel32.lib user32.lib >>C:\TEMP\build_log.txt 2>&1
if errorlevel 1 (echo LINK_FAIL >>C:\TEMP\build_log.txt & exit /b 1)

echo BUILD_OK >>C:\TEMP\build_log.txt
copy /Y d3d9.dll ..\bin\d3d9.dll >nul
del *.obj *.lib *.exp d3d9.dll 2>nul

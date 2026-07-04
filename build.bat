@echo off
echo =========================================
echo DOSTCG - Ultimate Build System
echo =========================================

set WATCOM=C:\WATCOM
set INCLUDE=%WATCOM%\h;%WATCOM%\h\nt

echo Cleaning old files...
if exist *.obj del *.obj
if exist game.exe del game.exe

echo.
echo Compiling C code...
"%WATCOM%\binnt\wcc.exe" main.c -mm -I"%WATCOM%\h"
if errorlevel 1 goto error
"%WATCOM%\binnt\wcc.exe" vga.c -mm -I"%WATCOM%\h"
if errorlevel 1 goto error

echo.
echo Linking game.exe...
"%WATCOM%\binnt\wlink.exe" system dos name game.exe file main.obj file vga.obj libpath "%WATCOM%\lib286\dos" libpath "%WATCOM%\lib286" library clibm.lib option stack=8192
if errorlevel 1 goto error
exit /b

:error
echo.
echo BUILD FAILED! Check the errors above.
pause
@echo off
rem build.bat [test]   -> ..\build\dinput8.dll (test: with the test harness, /DEDTEST)
rem embeds ..\build\text.bin (tools\make_text.py) and ..\build\fonts_roman.cfx (tools\make_font.py)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist ..\build mkdir ..\build
set DEFS=
if "%1"=="test" set DEFS=/DEDTEST
if not exist ..\build\text.bin echo EDJT> ..\build\text.bin
rc /nologo /fo ..\build\edjp.res edjp.rc || exit /b 1
cl /nologo /utf-8 /O2 /MT /W3 /LD /D_CRT_SECURE_NO_WARNINGS %DEFS% /Fo..\build\ edjp.c hook.c gfx.c text.c tips.c font.c inflate.c ..\build\edjp.res /Fe..\build\dinput8.dll /link /DEF:dinput8.def kernel32.lib user32.lib dxguid.lib ws2_32.lib || exit /b 1

@echo off
rem Builds with the Microsoft C compiler directly, no build system.
rem
rem 	build.bat [meth|tests|keydemo|all] [debug|release] [run]
rem
rem Run it from any prompt: when cl.exe is not on the path the newest Visual
rem Studio with the C++ tools is looked up with vswhere. Outputs:
rem build\direct\<config>\<program>.exe. "run" starts the program after the
rem build (the tests must run in this folder).
rem For an incremental build use premake5 (premake5 vs2022), see CLAUDE.md.

setlocal enabledelayedexpansion
cd /d "%~dp0"

set TARGET=all
set CONFIG=debug
set RUN=0

:args
if "%~1"=="" goto args_done
if /i "%~1"=="meth"  set TARGET=meth
if /i "%~1"=="tests"   set TARGET=tests
if /i "%~1"=="keydemo" set TARGET=keydemo
if /i "%~1"=="all"     set TARGET=all
if /i "%~1"=="debug"   set CONFIG=debug
if /i "%~1"=="release" set CONFIG=release
if /i "%~1"=="run"     set RUN=1
shift
goto args
:args_done

rem --- find the compiler ---
where cl >nul 2>nul
if errorlevel 1 (
	set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
	if not exist "!VSWHERE!" (
		echo cl.exe not found and no Visual Studio installation either.
		exit /b 1
	)
	for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
	if not defined VSDIR (
		echo No Visual Studio with the C++ tools found.
		exit /b 1
	)
	call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
)

set OUT=build\direct\%CONFIG%
set OBJ=%OUT%\obj

rem /MD(d): the same runtime as premake5.lua (staticruntime Off)
set FLAGS=/nologo /std:c11 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /Iinclude
if "%CONFIG%"=="release" (
	set FLAGS=%FLAGS% /O2 /MD /DNDEBUG
) else (
	set FLAGS=%FLAGS% /Od /Zi /MDd /DDEBUG
)

rem --- the library: one cl call per folder, objects in a folder of their own
rem (platform\config.c and platform\win32\config.c have the same name) ---
rem every file is compiled each time, and a folder that came or went would leave
rem the objects of another folder under its number
if exist "%OBJ%" rmdir /s /q "%OBJ%"
if not exist "%OBJ%" mkdir "%OBJ%"
set N=0
for /r "source\meth" /d %%d in (*) do (
	set "SKIP="
	echo %%d | findstr /i /c:"\platform\posix" >nul && set SKIP=1
	echo %%d | findstr /i /c:"\platform\wasm" >nul && set SKIP=1
	if not defined SKIP if exist "%%d\*.c" (
		set /a N+=1
		if not exist "%OBJ%\!N!" mkdir "%OBJ%\!N!"
		cl %FLAGS% /c "%%d\*.c" /Fo"%OBJ%\!N!\\" /Fd"%OBJ%\!N!\\" || exit /b 1
	)
)
if exist "source\meth\*.c" (
	set /a N+=1
	if not exist "%OBJ%\!N!" mkdir "%OBJ%\!N!"
	cl %FLAGS% /c "source\meth\*.c" /Fo"%OBJ%\!N!\\" /Fd"%OBJ%\!N!\\" || exit /b 1
)
dir /s /b "%OBJ%\*.obj" > "%OUT%\lib.rsp"

rem --- the programs ---
if /i "%TARGET%"=="meth" call :program meth app || exit /b 1
if /i "%TARGET%"=="all"    call :program meth app || exit /b 1
if /i "%TARGET%"=="tests"  call :program tests tests || exit /b 1
if /i "%TARGET%"=="all"    call :program tests tests || exit /b 1
if /i "%TARGET%"=="keydemo" call :program keydemo demo || exit /b 1
if /i "%TARGET%"=="all"    call :program keydemo demo || exit /b 1

if "%RUN%"=="1" (
	set RUNNAME=%TARGET%
	if /i "%TARGET%"=="all" set RUNNAME=tests
	"%OUT%\!RUNNAME!.exe"
)
exit /b %errorlevel%

rem call :program NAME SOURCEFOLDER
:program
echo cl -^> %OUT%\%1.exe
rem the objects of a program are kept out of obj\, which lib.rsp lists
if not exist "%OUT%\prog\%1" mkdir "%OUT%\prog\%1"
cl %FLAGS% "source\%2\*.c" @"%OUT%\lib.rsp" /Fo"%OUT%\prog\%1\\" /Fd"%OUT%\prog\%1\\" /Fe"%OUT%\%1.exe" || exit /b 1
exit /b 0

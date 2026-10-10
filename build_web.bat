@echo off
rem Builds the WebAssembly module of the web editor with clang, no other tools.
rem
rem 	build_web.bat
rem
rem Needs clang with the wasm32 target and wasm-ld (the LLVM release from
rem llvm.org); it is looked up on the path, then in "W:\Program Files\LLVM" and
rem "C:\Program Files\LLVM". Output: build\web\meth.wasm, copied to
rem web\meth.wasm. The library is built freestanding: platform\wasm\ has the
rem memory, the C library functions and the stubs for files and the console.

setlocal enabledelayedexpansion
cd /d "%~dp0"

set "CLANG="
where clang >nul 2>nul
if not errorlevel 1 set "CLANG=clang"
if "!CLANG!"=="" if exist "W:\Program Files\LLVM\bin\clang.exe" set "CLANG=W:\Program Files\LLVM\bin\clang.exe"
if "!CLANG!"=="" if exist "C:\Program Files\LLVM\bin\clang.exe" set "CLANG=C:\Program Files\LLVM\bin\clang.exe"
if "!CLANG!"=="" (
	echo clang.exe not found: install the LLVM release from llvm.org.
	exit /b 1
)

set OBJ=build\web\obj
if not exist %OBJ% mkdir %OBJ%

set FLAGS=--target=wasm32 -O2 -std=c11 -ffreestanding -fno-builtin -DML_WASM32 -Wall -Wextra -Wno-comment -Werror -Iinclude -isystem include\meth\platform\wasm\libc

set LIST=build\web\objects.rsp
if exist %LIST% del %LIST%

for /r source\meth %%f in (*.c) do (
	set "P=%%f"
	set "P=!P:%CD%\=!"
	set "SKIP="
	if not "!P:\platform\posix\=!"=="!P!" set SKIP=1
	if not "!P:\platform\win32\=!"=="!P!" set SKIP=1
	if "%%~nxf"=="meth.c" set SKIP=1
	if not defined SKIP (
		call :compile "!P!"
		if errorlevel 1 exit /b 1
	)
)

call :compile "source\web\api.c"
if errorlevel 1 exit /b 1

"!CLANG!" --target=wasm32 -nostdlib -fuse-ld=lld -Wl,--no-entry -Wl,-z,stack-size=1048576 -o build\web\meth.wasm @%LIST%
if errorlevel 1 exit /b 1
if not exist web mkdir web
copy /y build\web\meth.wasm web\meth.wasm >nul
echo build\web\meth.wasm

rem --- the C test groups that need no files, as a second module ---
set TFLAGS=%FLAGS% -Isource\tests -Wno-string-concatenation
set TLIST=build\web\tests.rsp
copy /y %LIST% %TLIST% >nul
for %%g in (core session lineedit rewrite brackets bool lambda bigint rational value lexer parser eval stats plot hyper matrix steps markup sym unit integrate words proof pga webapi) do (
	"!CLANG!" %TFLAGS% -c "source\tests\test_%%g.c" -o "%OBJ%\test_%%g.o"
	if errorlevel 1 exit /b 1
	echo build/web/obj/test_%%g.o>>%TLIST%
)
"!CLANG!" %TFLAGS% -c "source\web\tests.c" -o "%OBJ%\web_tests.o"
if errorlevel 1 exit /b 1
echo build/web/obj/web_tests.o>>%TLIST%
"!CLANG!" --target=wasm32 -nostdlib -fuse-ld=lld -Wl,--no-entry -Wl,-z,stack-size=4194304 -o build\web\tests.wasm @%TLIST%
if errorlevel 1 exit /b 1
copy /y build\web\tests.wasm web\tests.wasm >nul
echo build\web\tests.wasm
exit /b 0

:compile
rem %1 is a source file (relative); the object is named after its whole path
set "SRC=%~1"
set "NAME=!SRC:\=_!"
"!CLANG!" %FLAGS% -c "%SRC%" -o "%OBJ%\!NAME!.o"
if errorlevel 1 exit /b 1
rem the list file reads a backslash as an escape: slashes there
set "ENTRY=%OBJ%\!NAME!.o"
echo !ENTRY:\=/!>>%LIST%
exit /b 0

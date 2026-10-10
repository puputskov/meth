#!/bin/sh
# Builds the WebAssembly modules of the web editor with clang, no other tools.
#
#	./build_web.sh
#
# Needs clang with the wasm32 target and wasm-ld (the packages clang and lld on
# Debian and Ubuntu, or the LLVM release from llvm.org). CLANG picks the
# compiler (default clang). Outputs build/web/meth.wasm and build/web/tests.wasm,
# copied to web/. The library is built freestanding: platform/wasm/ has the
# memory, the C library functions and the stubs for files and the console.
# build_web.bat does the same on Windows.

set -eu

cd "$(dirname "$0")"

CLANG=${CLANG:-clang}
obj=build/web/obj
flags="--target=wasm32 -O2 -std=c11 -ffreestanding -fno-builtin -DML_WASM32 -Wall -Wextra -Wno-comment -Werror -Iinclude -isystem include/meth/platform/wasm/libc"

rm -rf "$obj"
mkdir -p "$obj"
: > build/web/objects.rsp

# compile FILE EXTRA-FLAGS LIST: the object is named after the whole path of
# the file (the same file name is in several folders) and added to LIST
compile() {
	name=$(echo "$1" | tr '/' '_')
	"$CLANG" $flags $2 -c "$1" -o "$obj/$name.o"
	echo "$obj/$name.o" >> "$3"
}

for f in $(find source/meth -name '*.c' ! -path '*/platform/posix/*' ! -path '*/platform/win32/*' ! -name meth.c | sort); do
	compile "$f" "" build/web/objects.rsp
done

compile source/web/api.c "" build/web/objects.rsp

"$CLANG" --target=wasm32 -nostdlib -fuse-ld=lld -Wl,--no-entry -Wl,-z,stack-size=1048576 \
	-o build/web/meth.wasm @build/web/objects.rsp
mkdir -p web
cp build/web/meth.wasm web/meth.wasm
echo build/web/meth.wasm

# the C test groups that need no files, as a second module
cp build/web/objects.rsp build/web/tests.rsp

for g in core session lineedit rewrite brackets bool lambda bigint rational value lexer parser eval stats plot hyper matrix steps markup sym unit integrate words proof pga webapi; do
	compile "source/tests/test_$g.c" "-Isource/tests -Wno-string-concatenation" build/web/tests.rsp
done

compile source/web/tests.c "-Isource/tests -Wno-string-concatenation" build/web/tests.rsp

"$CLANG" --target=wasm32 -nostdlib -fuse-ld=lld -Wl,--no-entry -Wl,-z,stack-size=4194304 \
	-o build/web/tests.wasm @build/web/tests.rsp
cp build/web/tests.wasm web/tests.wasm
echo build/web/tests.wasm

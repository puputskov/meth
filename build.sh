#!/bin/sh
# Builds with the C compiler directly, no build system. One compiler call per
# program, with every source file on the command line.
#
#	./build.sh [meth|tests|keydemo|all] [debug|release] [run]
#
# CC picks the compiler (default cc). Outputs: build/direct/<config>/<program>.
# "run" starts the program after the build ("tests" must run in this folder).
# For a faster, incremental build use `make`.

set -eu

cd "$(dirname "$0")"

target=all
config=debug
run=0

for arg in "$@"; do
	case "$arg" in
		meth|tests|keydemo|all) target=$arg ;;
		debug|release) config=$arg ;;
		run) run=1 ;;
		*)
			echo "usage: $0 [meth|tests|keydemo|all] [debug|release] [run]" >&2
			exit 2
			;;
	esac
done

CC=${CC:-cc}
out=build/direct/$config
mkdir -p "$out"

if [ "$config" = release ]; then
	opt="-O2 -DNDEBUG"
else
	opt="-O0 -g -DDEBUG"
fi

# The platform layer: Linux and macOS use platform/posix, not platform/win32.
lib=$(find source/meth -name '*.c' ! -path '*/platform/win32/*' | sort)

# A warning is an error, as in the Makefile and premake5.lua.
flags="-std=gnu11 -Wall -Wextra -Wno-comment -Werror -Iinclude $opt"

build() {
	name=$1
	shift
	echo "$CC -> $out/$name"
	# shellcheck disable=SC2086
	$CC $flags "$@" $lib -lm -o "$out/$name"
}

case "$target" in
	meth|all) build meth source/app/*.c ;;
esac
case "$target" in
	tests|all) build tests source/tests/*.c ;;
esac
case "$target" in
	keydemo|all) build keydemo source/demo/*.c ;;
esac

if [ "$run" = 1 ]; then
	if [ "$target" = all ]; then
		target=tests
	fi
	"$out/$target"
fi

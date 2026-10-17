#!/bin/sh
# Runs the LLVM discovery block of the top-level CMakeLists.txt (everything before find_package(LLVM))
# against fake llvm-config trees: a pinned LLVM_DIR reached through a symlink must be left alone, and a
# stale LLVM_DIR must fall back to the llvm-config on PATH.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top="$here/../../CMakeLists.txt"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fake_config() { # dir cmakedir
	mkdir -p "$1/bin"
	printf '#!/bin/sh\ncase "$*" in\n*--cmakedir*) echo "%s";;\n*) echo libLLVM-99.so;;\nesac\n' "$2" >"$1/bin/llvm-config"
	chmod +x "$1/bin/llvm-config"
}

# the pinned LLVM: <real>/lib/cmake/llvm with <real>/bin/llvm-config, reached through a symlinked cmake dir
# that sits in a prefix without a bin/llvm-config of its own
mkdir -p "$tmp/real/lib/cmake/llvm" "$tmp/link/lib/cmake" "$tmp/other/cmake"
fake_config "$tmp/real" "$tmp/real/lib/cmake/llvm"
ln -s "$tmp/real/lib/cmake/llvm" "$tmp/link/lib/cmake/llvm-99"
# the llvm-config on PATH
fake_config "$tmp/path" "$tmp/other/cmake"

mkdir "$tmp/proj"
{
	echo 'cmake_minimum_required(VERSION 3.20)'
	echo 'project(llvm-discovery NONE)'
	sed -n '/^function(indium_llvm_config_works/,/^find_package(LLVM REQUIRED CONFIG)/p' "$top" | sed '$d'
	echo 'message(STATUS "RESULT LLVM_DIR=${LLVM_DIR}")'
} >"$tmp/proj/CMakeLists.txt"

run() { # LLVM_DIR value
	PATH="$tmp/path/bin:$PATH" cmake -S "$tmp/proj" -B "$tmp/build-$$-$1" "-DLLVM_DIR=$2" 2>&1 | sed -n 's/^-- RESULT LLVM_DIR=//p'
}

fail=0
got=$(run symlink "$tmp/link/lib/cmake/llvm-99")
[ "$got" = "$tmp/link/lib/cmake/llvm-99" ] || { echo "FAIL: symlinked pinned LLVM_DIR was replaced by '$got'"; fail=1; }
got=$(run stale "$tmp/gone/lib/cmake/llvm")
[ "$got" = "$tmp/other/cmake" ] || { echo "FAIL: stale LLVM_DIR resolved to '$got', want $tmp/other/cmake"; fail=1; }
[ $fail = 0 ] && echo PASS
exit $fail

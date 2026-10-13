#!/bin/sh
# Usage: check.sh <physical-gep binary> <scratch dir>
# Translates each authored shape and compares the getelementptr byte constants Iridium
# emitted with the LLVM DataLayout values the fixture prints. R rows also run spirv-val
# (their readback is manual, see README.md); C rows are constant-only because Iridium
# declares no 8/16-bit storage or Vector16 capability; E rows must fail translation.
set -u
gep=$1
out=$2
mkdir -p "$out"
failures=0
fail() { echo "FAIL $*"; failures=$((failures + 1)); }

# The GEP byte constants in instruction order: first-index multiplier, then each step.
constants() {
	spirv-dis "$1" | grep -oE 'Op(IMul|IAdd) %ulong %[A-Za-z0-9_]+ %ulong_[0-9]+' | grep -oE '[0-9]+$' | paste -sd' ' -
}

# check <R|C> <shape> [extended regex the disassembly must contain]...
check() {
	oracle=$1 shape=$2
	shift 2
	spv="$out/$shape.spv"
	if ! line=$("$gep" "$shape" "$spv" 2>"$out/$shape.err"); then
		fail "$shape translate: $(cat "$out/$shape.err")"
		return
	fi
	want=${line#expect }
	got=$(constants "$spv")
	if [ "$got" != "$want" ]; then
		fail "$shape constants '$got' want '$want'"
		return
	fi
	for pattern; do
		if ! spirv-dis "$spv" | grep -qE "$pattern"; then
			fail "$shape has no '$pattern'"
			return
		fi
	done
	if [ "$oracle" = R ] && ! spirv-val --target-env vulkan1.3 --relax-block-layout --scalar-block-layout "$spv"; then
		fail "$shape spirv-val"
		return
	fi
	echo "PASS $shape $got"
}

# differs <shape> <constants>: negative control, the comparison must reject a wrong value
differs() {
	got=$(constants "$out/$1.spv")
	if [ -n "$got" ] && [ "$got" != "$2" ]; then
		echo "PASS $1 rejects '$2'"
	else
		fail "$1 accepted '$2'"
	fi
}

check R vector
check R scalar
check R array
check R array32
check R vec3
check R neg32 'OpSConvert %long %int_n1'
check R struct-vec3
check R nested-vec3
check C struct-half3
check C struct-int3
check R struct-lead
check R struct-vec4
check R struct-vec2
differs vec3 12

echo "$failures failures"
[ "$failures" -eq 0 ]

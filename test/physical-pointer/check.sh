#!/bin/sh
# Usage: check.sh <mtl2spv> <scratch dir>
# Translates physical-pointer.metallib, validates it and checks that every device pointer it computes or loads has the
# one type Iridium gives them; then checks that each library in refused/ fails with the message its .ll names in a
# "; refused: <text>" line.
set -u
mtl2spv=$1
out=$2
here=$(dirname "$0")
mkdir -p "$out"
failures=0
fail() { echo "FAIL $*"; failures=$((failures + 1)); }

spv="$out/physical-pointer.spv"
if "$mtl2spv" "$here/physical-pointer.metallib" "$spv" >"$out/physical-pointer.log" 2>&1 &&
	spirv-val --target-env vulkan1.3 "$spv" >>"$out/physical-pointer.log" 2>&1 &&
	spirv-dis "$spv" >"$out/physical-pointer.spvasm" 2>>"$out/physical-pointer.log"; then
	echo "PASS physical-pointer.metallib"
else
	fail "physical-pointer.metallib: $(cat "$out/physical-pointer.log")"
fi

asm="$out/physical-pointer.spvasm"
# expect <description> <awk program>: the program reads the disassembly and exits with 0 when it holds
expect() {
	if awk "$2" "$asm"; then echo "PASS $1"; else fail "$1"; fi
}
t='%_ptr_PhysicalStorageBuffer_uint'
expect "buffer arguments are uint pointers" "
	/OpTypePointer Uniform %_ptr_PhysicalStorageBuffer_/ { if (\$NF != \"$t\") bad = 1; found = 1 }
	END { exit bad || !found }"
expect "getelementptr results are uint pointers" "
	/OpConvertUToPtr / { if (\$4 != \"$t\") bad = 1; found = 1 }
	END { exit bad || !found }"
expect "a loaded address is a uint pointer" "
	/OpLoad %_ptr_PhysicalStorageBuffer_/ && / Aligned 8\$/ { if (\$4 != \"$t\") bad = 1; found = 1 }
	END { exit bad || !found }"
expect "an address is stored as a uint pointer" "
	\$3 == \"OpBitcast\" && \$4 == \"%_ptr_PhysicalStorageBuffer_${t#%}\" { slot[\$1] = 1 }
	\$1 == \"OpStore\" && slot[\$2] && \$NF == 8 { found = 1 }
	END { exit !found }"
# offsetof(Array, scale): getelementptr %struct.Array, %array, 0, 1 is 0 * 16 + 8
expect "a field after an address is 8 bytes in" "
	\$3 == \"OpIMul\" && \$6 == \"%ulong_16\" { first[\$1] = 1 }
	\$3 == \"OpIAdd\" && first[\$5] && \$6 == \"%ulong_8\" { found = 1 }
	END { exit !found }"

for lib in "$here"/refused/*.metallib; do
	name=$(basename "$lib" .metallib)
	want=$(sed -n 's/^; refused: //p' "$here/refused/$name.ll")
	if "$mtl2spv" "$lib" "$out/$name.spv" >"$out/$name.log" 2>&1; then
		fail "$name translated"
	elif grep -qF "$want" "$out/$name.log"; then
		echo "PASS $name refused"
	else
		fail "$name: $(cat "$out/$name.log")"
	fi
done

echo "$failures failures"
[ "$failures" -eq 0 ]

#!/usr/bin/env python3
"""Build the authored fixture using an existing Darling build's AppKit flags."""
import argparse
import pathlib
import shlex
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("build", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
args = parser.parse_args()
build = args.build.resolve()
output = args.output.resolve()
output.parent.mkdir(parents=True, exist_ok=True)
commands = subprocess.check_output(
    ["ninja", "-C", str(build), "-t", "commands", "AppKit"], text=True
).splitlines()
command = next(c for c in commands if " -c " in c and "/NSView.m" in c)
original = shlex.split(command)
compile_args = []
index = 0
while index < len(original):
    value = original[index]
    if value in ("-o", "-c", "-MT", "-MF"):
        index += 2
        continue
    if value in ("-MD", "-MMD"):
        index += 1
        continue
    compile_args.append(value)
    index += 1
source = pathlib.Path(__file__).with_name("main.mm").resolve()
obj = output.with_suffix(".o")
subprocess.run(compile_args + ["-c", str(source), "-o", str(obj)], cwd=build, check=True)
link_command = next(c for c in commands if " -o src/external/cocotron/AppKit/AppKit " in c)
maps = [a for a in shlex.split(link_command) if "-dylib_file," in a]
target = compile_args[compile_args.index("-target") + 1]
linker = build / "host-tools-build/ld64" / (target + "-ld")
libraries = [
    "src/external/cocotron/AppKit/AppKit",
    "src/external/cocotron/QuartzCore/QuartzCore",
    "src/external/foundation/Foundation",
    "src/external/objc4/runtime/libobjc.A.dylib",
    "src/external/libsystem/libSystem.B.dylib",
    "src/external/metal/Metal",
]
subprocess.run(
    [compile_args[0], "-target", target, "-nostdlib", "-fuse-ld=" + str(linker),
     "-Wl,-Z", "-Wl,-sdk_version,11.0", "-Wl,-platform_version,macos,11.0,11.0",
     *maps, "-o", str(output), str(obj), *libraries], cwd=build, check=True
)

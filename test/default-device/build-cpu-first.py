import pathlib, shlex, subprocess, sys
build=pathlib.Path(sys.argv[1]).resolve()
source=pathlib.Path(__file__).with_name('cpu-first.cpp').resolve(); output=pathlib.Path(sys.argv[2]).resolve()
output.parent.mkdir(parents=True,exist_ok=True)
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','indium'],text=True).splitlines()
compile_line=next(c for c in commands if ' -c ' in c and '/device.cpp' in c)
args=shlex.split(compile_line); clean=[]; i=0
while i<len(args):
    a=args[i]
    if a in ('-o','-c','-MT','-MF'): i+=2; continue
    if a in ('-MD','-MMD'): i+=1; continue
    clean.append(a); i+=1

obj=output.with_suffix('.o')
subprocess.run(clean+['-c',str(source),'-o',str(obj)],cwd=build,check=True)
link_line=next(c for c in commands if ' -o src/external/metal/deps/indium/libindium.dylib ' in c)
maps=[a for a in shlex.split(link_line) if '-dylib_file,' in a]
libs=['src/external/metal/deps/indium/libindium.dylib','src/external/libcxx/libc++.1.dylib','src/external/libsystem/libSystem.B.dylib', *sys.argv[3:]]
subprocess.run(['/usr/bin/clang','-target','aarch64-apple-darwin20','-nostdlib',
    '-fuse-ld='+str(build/'host-tools-build/ld64/aarch64-apple-darwin20-ld'),'-Wl,-Z','-Wl,-sdk_version,11.0',
    '-Wl,-platform_version,macos,11.0,11.0',*maps,'-o',str(output),str(obj),*libs],cwd=build,check=True)
print('built',output)

import pathlib, shlex, subprocess, sys
build=pathlib.Path(sys.argv[1]).resolve(); repo=pathlib.Path(__file__).resolve().parents[2]
source=pathlib.Path(sys.argv[2]).resolve(); output=pathlib.Path(sys.argv[3]).resolve(); library=pathlib.Path(sys.argv[4]).resolve()
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','indium'],text=True).splitlines()
compile_line=next(c for c in commands if ' -c ' in c and '/device.cpp' in c)
args=shlex.split(compile_line); clean=[args[0],'-I'+str(repo/'include'),'-I'+str(repo/'private-include')]; i=1
while i<len(args):
    a=args[i]
    if a in ('-o','-c','-MT','-MF'): i+=2; continue
    if a in ('-MD','-MMD'): i+=1; continue
    clean.append(a); i+=1

obj=output.with_suffix('.o')
subprocess.run(clean+['-c',str(source),'-o',str(obj)],cwd=build,check=True)
link_line=next(c for c in commands if ' -o src/external/metal/deps/indium/libindium.dylib ' in c)
maps=[a for a in shlex.split(link_line) if '-dylib_file,' in a]
libs=[str(library),'src/external/libcxx/libc++.1.dylib','src/external/libsystem/libSystem.B.dylib']
subprocess.run(['/usr/bin/clang','-target','aarch64-apple-darwin20','-nostdlib',
    '-fuse-ld='+str(build/'host-tools-build/ld64/aarch64-apple-darwin20-ld'),'-Wl,-Z','-Wl,-sdk_version,11.0',
    '-Wl,-platform_version,macos,11.0,11.0',*maps,'-o',str(output),str(obj),*libs],cwd=build,check=True)
print('built',output)

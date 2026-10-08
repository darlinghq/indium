These authored fixtures exercise Indium and public Metal API calls; no compiled shader inputs are used. `cpu-first.cpp` enumerates real Vulkan devices, then wraps Darling's own Vulkan dispatch pointer to place CPU devices first before rerunning device initialization. It requires an accepted CPU driver and an accepted integrated/discrete GPU driver, otherwise exits2. Baseline selects the CPU and exits1; the candidate selects a GPU and exits0. The normal driver order is deliberately not a regression: on this host it already selects AppleM1.

Build against a configured Darling tree, under the shared build lock:

```sh
flock /tmp/agent-locks/darling-heavy-build.lock python3 test/default-device/build-cpu-first.py <darling-build> <scratch>/cpu-first
flock /tmp/agent-locks/darling-heavy-build.lock python3 test/default-device/build.py <darling-build> <scratch>/default-device
```

Launch using your build-tree non-setuid launcher, private image and independent prefix:

```sh
env -u VK_DRIVER_FILES -u VK_ICD_FILENAMES DPREFIX=<prefix> DARLING_INSTALL_PREFIX=<image>/usr/local <launcher> shell /Volumes/SystemRoot<scratch>/cpu-first
env DPREFIX=<prefix> DARLING_INSTALL_PREFIX=<image>/usr/local <launcher> shell env DARLING_ENABLE_METAL=1 /Volumes/SystemRoot<scratch>/default-device 'Apple M1 (G13G B1)'
```

Adapt the expected name to a device reported by the native Vulkan loader. Metal currently exposes only its selected default through MTLCopyAllDevices, so that call does not prove all Vulkan adapters were considered. A CPU-only control uses host and guest VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json and expects the reported llvmpipe name; candidate exit0 preserves the software fallback.

Recovered policy source: preserved OSS Indium9355dd3. Apple public `Getting the default GPU` specifies a discrete GPU on multi-GPU Macs: https://developer.apple.com/documentation/metal/getting-the-default-gpu . Integrated/virtual/software order is Darling's conservative Vulkan fallback policy; display-specific and external-GPU preference are outside this change. Native DodgeDanger still lacks menu/text/game geometry; default-device selection alone is not app success.

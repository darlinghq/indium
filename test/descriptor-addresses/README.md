# Descriptor address table regression

This authored C++ fixture exercises the real `createDescriptorSets` template and
Vulkan descriptor allocation on a Darling Indium device. It needs no guest shader
or proprietary input. The sparse and multiple cases bind slot 2 while declaring
one or two shader buffer bindings; they check table length, binding order, and
buffer offsets. The empty case binds an unused slot but declares no buffer binding.
The dense case is the unchanged control.

Build against a populated Darling build using its pinned compiler/linker recipe:

```sh
flock /tmp/agent-locks/darling-heavy-build.lock python3 test/descriptor-addresses/build.py   /home/cristi/tmp-opencode/metal-resume/build   /home/cristi/tmp-opencode/metal-resume/descriptor-address-evidence/final-candidate-test
```

Run each of `dense`, `sparse`, `multi`, and `empty`:

```sh
env VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json   DPREFIX=/home/cristi/tmp-opencode/metal-resume/descriptor-address-evidence/candidate-prefix   DARLING_INSTALL_PREFIX=/home/cristi/tmp-opencode/metal-resume/image/usr/local   /home/cristi/tmp-opencode/metal-resume/build/src/startup/darling shell env   VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json   /Volumes/SystemRoot/home/cristi/tmp-opencode/metal-resume/descriptor-address-evidence/final-candidate-test sparse
```

For the baseline, save the parent commit's `command-encoder.private.hpp` under an
independent `baseline-include/indium` directory, then set
`INDIUM_TEST_PRIVATE_INCLUDE` to `baseline-include` during the identical build.
No library or shared source replacement is needed: the template is instantiated
in the fixture. On native Apple M1, baseline dense exits 0; sparse/multi exit 1
with incorrect 24-byte tables. Fixed dense/sparse/multi/empty exit 0 with table
sizes 8/8/16/0 and matching addresses. Baseline empty produced SIGSEGV, confirmed
by crash metadata before prefix shutdown. Shut down each owned prefix with the
same non-setuid launcher and environment.

Verification donor was committed main65c272fdc, Metal3e11a97,
Indiumcd83778, Cocotron1aeb6d43. Other include paths, libraries, and compiler
options come from that build. The `INDIUM_TEST_PRIVATE_INCLUDE` override changes
only which template header the regression instantiates. This test establishes
address-table safety; actual application pixels and input remain separate checks.

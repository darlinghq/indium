# Completion-wait contract

Apple's public `MTLCommandBuffer.waitUntilCompleted` contract requires both GPU
completion and the return of all registered completed handlers:
https://developer.apple.com/documentation/metal/mtlcommandbuffer/waituntilcompleted()

This independently written native Vulkan test holds a completed handler on a
condition variable after a real command-buffer submission has finished on the
GPU. Two client threads announce their wait and call `waitUntilCompleted`.
Neither may return while that handler is held. The fixture holds the first and
the last of three registered handlers, then releases the gate and verifies
both waiters return and the callbacks drain exactly once in registration order.

The 150 ms observation is bounded evidence that no caller returns early, not
proof of the exact kernel state of each waiter. The fixture drains the callback
queue before destroying state, including when run against the broken version.
It also preserves the existing nested-buffer and scheduled-handler waits when
the target buffer has no completed handlers; waiting for queue retirement in
that case would deadlock the handler thread.

Build/run from the repository root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
cmake --build build --target indium-test-command-completion --parallel 4
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json ctest --test-dir build -R '^command-completion$' --output-on-failure
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json ctest --test-dir build -R '^command-completion$' --output-on-failure
```

No shaders, external assets or new dependencies are needed for this fixture.
CTest skips it with return code 77 if no Vulkan device is available.

Provenance: the public Metal contract and independently authored test inputs.
No restricted Apple source, Apple binary inspection or third-party test code
was used.

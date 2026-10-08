This authored native test holds the Vulkan queue behind a timeline semaphore.
Scheduled callbacks must run before its release; GPU completion must remain held.
The original old-interface fixture failed this check on Apple M1: scheduled
callbacks ran only after GPU completion.

The expanded test requires the new interface and matching complete Indium build.
Two caller threads announce entry before calling waitUntilScheduled; both must
remain unreturned during a 100 ms observation while a scheduled callback is held.
This is a bounded scheduling observation, not proof of each thread's kernel state.
After releasing the callback both callers must return while the GPU remains held.
A second callback waits for GPU completion, exercising event-poller reentrancy.
Callback counts must remain exactly one.

Compile with the project's arm64 Indium flags and link the fully rebuilt library;
do not run the new virtual call against the old library. All Indium allocation
callers must rebuild because PrivateCommandBuffer size changes. Run with host and
guest VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json in a private prefix.

Specification: Apple's public waitUntilScheduled documentation requires GPU
scheduling and completion of all registered scheduled handlers, independently
of GPU execution completion. Escaping callback exceptions explicitly terminate;
this implementation does not silently report success after abandoning handlers.
The pre-existing waitUntilCompleted notification-before-handler behavior is out
of scope.

Exact measured private build/run commands (after rebuilding all 19 Indium and
37 matching Metal units into scheduling-build) from this repository root:

```sh
scheduling_root=/home/cristi/tmp-opencode/metal-resume
flock /tmp/agent-locks/darling-heavy-build.lock python3 test/command-scheduling/build.py "$scheduling_root/build" test/command-scheduling/scheduled.cpp "$scheduling_root/scheduling-build/scheduled" "$scheduling_root/scheduling-build/libindium.dylib"
env VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json DPREFIX="$scheduling_root/scheduling-health-donor-prefix" DARLING_INSTALL_PREFIX="$scheduling_root/runtime-scheduling-57e7993/image/usr/local" "$scheduling_root/runtime-scheduling-57e7993/darling" shell env VK_DRIVER_FILES=/usr/share/vulkan/icd.d/asahi_icd.json DARLING_ENABLE_METAL=1 /Volumes/SystemRoot/home/cristi/tmp-opencode/metal-resume/scheduling-build/scheduled
env DPREFIX="$scheduling_root/scheduling-health-donor-prefix" DARLING_INSTALL_PREFIX="$scheduling_root/runtime-scheduling-57e7993/image/usr/local" "$scheduling_root/runtime-scheduling-57e7993/darling" shutdown
```

Candidate: Apple M1, all four PASS lines, exit0. Public Metal selector client
also returned0 instead of baseline NSInvalidArgumentException1. Prefix shutdown0;
some prior launches required shutdown/reset after dead-server cleanup. No
long-term runtime stability claim.

For fail-before, build before.cpp (uses only the pre-existing interface) with
this helper and the unmodified library, then run in an unmodified private runtime.
Its timeline gate produces FAIL/exit1: scheduled notification is absent before
GPU release, completion remains blocked, both callbacks drain afterward. Never
run scheduled.cpp's new virtual method against that old library.

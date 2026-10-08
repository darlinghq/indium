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

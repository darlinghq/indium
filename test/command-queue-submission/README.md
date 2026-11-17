# command-queue-submission

How a command queue takes command buffers, submits them and reports their progress. Metal documents that

- `commit` returns without waiting for the GPU, and a queue runs its command buffers in the order they were enqueued
  (`enqueue` reserves the place, `commit` enqueues a buffer that has not been);
- the scheduled handlers of a command buffer are called once it is scheduled and the completed handlers once it has
  finished, on a thread of the system's, never on the one that committed, and `waitUntilScheduled` and
  `waitUntilCompleted` wake threads other than the one that committed;
- a command buffer holds the resources it references only until it has finished.

The test holds the Vulkan queue behind a timeline semaphore where it needs work to be submitted but not finished, and
checks: `commit` returns while a scheduled handler is blocked; the handlers of the command buffers of one queue run in
order (scheduled before completed for each, completed in commit order, also with several threads committing), not on the
committing thread; enqueue order beats commit order, and an enqueued buffer that is dropped does not hold the queue
back; handlers may commit and wait for other command buffers; the buffers a command buffer references are released when
it has finished although the client keeps the command buffer, including with thousands of command buffers in flight.

`compute.spv` is the module of test/hazard-tracking. Prints SKIP and exits 0 without a Vulkan device.

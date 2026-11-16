# events-fences

Events, shared events and fences:

- a command buffer's `encodeSignalEvent()` / `encodeWaitForEvent()` order work across command buffers, queues and
  within one command buffer, whatever order the command buffers are committed in; a wait on a value that has
  already been reached holds nothing up; a signal that does not raise the value changes nothing;
- the host raising a shared event's value lets a waiting command buffer go on, and a command buffer behind a
  waiting one on the same queue stays behind it while other queues carry on;
- `notify()`, `waitUntilSignaledValue()`, `signaledValue()` / `setSignaledValue()` and shared event handles;
- a fence orders an encoder after the encoder that updated it, a wait in a compute or blit encoder records a
  memory dependency, and a fence of no device or an event used while an encoder is active are refused.

The module is the one of `test/indirect-draw-dispatch` (only its `mark` kernel is used). Skips (prints SKIP,
exits 0) when there is no Vulkan device.

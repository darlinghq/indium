# hazard-tracking

How the commands of a command buffer are ordered against each other. Metal orders the encoders of a command buffer for
the resources it tracks (the default for resources made from a device, and for heaps made tracked), and leaves
resources made untracked to the application's fences, events and memory barriers. Indium records only the barriers
the accesses need:

- one barrier per read-after-write, write-after-read and write-after-write between the commands that touch the same
  memory, none between commands that touch different memory or only read it;
- the dispatches of a serial compute encoder are ordered by what they access, those of a concurrent one are not (a
  memory barrier orders them);
- texture views, textures made over a buffer and heap resources are the memory they alias;
- resources declared with `useResource` and `useHeap` are accessed by the dispatches that follow;
- untracked resources get no barrier of their own: a fence wait gives them one.

The test hooks `vkCmdPipelineBarrier2` and checks which barriers each sequence of encoders records (the mechanism), and
runs chains of dependent work and checks the numbers they produce (the result). On a device that serialises everything
(lavapipe) only the barrier checks can fail when a barrier is missing.

`compute.spv` and `graphics.spv` are mslc's output for the `.metal` files next to them:

    mslc -o compute.spv compute.metal
    mslc -o graphics.spv graphics.metal

Skips (prints SKIP, exits 0) when there is no Vulkan device.

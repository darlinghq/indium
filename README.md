# Indium
A Metal implementation on top of Vulkan. Implements a C++ API similar to the official Metal C++ API. This is meant to be a library used internally by
an ABI-compatible implementation of Metal. It is NOT a drop-in replacement
for Metal in an existing codebase.

## Vulkan and Device Requirements

The current implementation targets Vulkan 1.3. This version of Vulkan includes
several dynamic pipeline features which allow us to match the behavior of
several similar Metal features. Alternatively, the
`VK_EXT_extended_dynamic_state` and `VK_EXT_extended_dynamic_state2` features
should also be enough, though support for these extensions as substitutes for
Vulkan 1.3 support is not currently implemented.

Indium also requires timeline semaphore support (supported by most devices).

## Important API Distinctions

Because this library isn't meant to depend on Foundation or CoreFoundation or
even an Objective-C runtime, we do have to deviate from the official C++ in some
notable ways. For example, where the official API returns `NSArray`, we return
`std::vector`; where it returns `NSString`, we return `std::string`; where
Clang blocks are used, `std::function`s are used instead.
Additionally, instead of using manual reference counting tied to Objective-C's
`retain` and `release` methods, we use C++ `shared_ptr`s.

Additionally, Metal's official C++ API is essentially a thin wrapper over the
Objective-C Metal API. As such, all its structures, including mutable
descriptor structures only used to create other objects, are created as shared
objects. In contrast, Indium's descriptor structures are not created as shared
objects since these aren't required to stick around; we only need them to hold
user information for creating other objects. An ABI-compatible Metal
implementation that uses Indium internally is expected to implement its own
(shared) versions of these descriptors and translate their information into
an Indium descriptor to pass it to a method that needs it.

## IndiumKit

This repository also includes a supplementary library called IndiumKit which
aims to replicate the MetalKit API. It's mainly intended to be used for testing
code that uses Indium and not for actual use e.g. in an ABI-compatible
implementation of Metal that uses Indium. Most of the MetalKit API requires
implementation details that an ABI-compatible implementation of Metal would need
to implement itself.

## Iridium

Iridium is a library that can be used independently of Indium for translating
pre-compiled Metal shaders (in the AIR format) to SPIR-V shaders that Indium
can use.

Indium uses Iridium internally to translate shaders at runtime, but a CLI tool
for translating shaders called `mtl2spv` is also included in this repository,
mainly for testing purposes.

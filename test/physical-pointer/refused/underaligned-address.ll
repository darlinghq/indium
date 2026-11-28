; Copyright (C) 2026 Darling Developers
;
; Loads a device address with 4-byte alignment; Vulkan requires 8, and Iridium doesn't assume more than the module says.
; refused: device memory access of an address with alignment 4; addresses need 8

source_filename = "underaligned-address"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @underaligned_address(ptr addrspace(1) %out, ptr addrspace(1) %array) {
  %data = load ptr addrspace(1), ptr addrspace(1) %array, align 4
  %v = load i32, ptr addrspace(1) %data, align 4
  store i32 %v, ptr addrspace(1) %out, align 4
  ret void
}

!air.version = !{!0}
!air.language_version = !{!1}
!0 = !{i32 2, i32 2, i32 0}
!1 = !{!"Metal", i32 2, i32 2, i32 0}
!2 = !{}
!air.kernel = !{!10}
!10 = !{ptr @underaligned_address, !2, !11}
!11 = !{!12, !13}
!12 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}
!13 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"Array", !"air.arg_name", !"array"}

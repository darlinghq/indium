; Copyright (C) 2026 Darling Developers
;
; Stores the address of a program-scope constant into device memory. Iridium declares the constant as a SPIR-V
; variable, which has no address.
; refused: store of a pointer that is not a device or constant address into device memory

source_filename = "stored-variable"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

@table = internal addrspace(2) constant [2 x i32] [i32 1, i32 2], align 4

define void @stored_variable(ptr addrspace(1) %out) {
  store ptr addrspace(2) @table, ptr addrspace(1) %out, align 8
  ret void
}

!air.version = !{!0}
!air.language_version = !{!1}
!0 = !{i32 2, i32 2, i32 0}
!1 = !{!"Metal", i32 2, i32 2, i32 0}
!2 = !{}
!air.kernel = !{!10}
!10 = !{ptr @stored_variable, !2, !11}
!11 = !{!12}
!12 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 8, !"air.arg_type_name", !"Slot", !"air.arg_name", !"out"}

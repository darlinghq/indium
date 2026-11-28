; Copyright (C) 2026 Darling Developers
;
; Samples through a sampler handle, as a Metal argument buffer holds one, loaded from device memory: a device address isn't a sampler.
; refused: sampler operand is not a sampler argument

source_filename = "loaded-sampler"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @loaded_sampler(ptr addrspace(1) %out, ptr addrspace(1) %handles, ptr addrspace(1) %t) {
  %s = load ptr addrspace(2), ptr addrspace(1) %handles, align 8
  %sample = call { <4 x float>, i8 } @air.sample_texture_2d.v4f32(ptr addrspace(1) %t, ptr addrspace(2) %s, <2 x float> <float 5.000000e-01, float 5.000000e-01>, i1 true, <2 x i32> zeroinitializer, i1 true, float 0.000000e+00, float 0.000000e+00, i32 0)
  %texel = extractvalue { <4 x float>, i8 } %sample, 0
  store <4 x float> %texel, ptr addrspace(1) %out, align 16
  ret void
}

declare { <4 x float>, i8 } @air.sample_texture_2d.v4f32(ptr addrspace(1), ptr addrspace(2), <2 x float>, i1, <2 x i32>, i1, float, float, i32)

!air.version = !{!0}
!air.language_version = !{!1}
!0 = !{i32 2, i32 2, i32 0}
!1 = !{!"Metal", i32 2, i32 2, i32 0}
!2 = !{}
!air.kernel = !{!10}
!10 = !{ptr @loaded_sampler, !2, !11}
!11 = !{!12, !13, !14}
!12 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4", !"air.arg_name", !"out"}
!13 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 8, !"air.arg_type_name", !"Handles", !"air.arg_name", !"handles"}
!14 = !{i32 2, !"air.texture", !"air.location_index", i32 0, i32 1, !"air.sample", !"air.arg_type_name", !"texture2d<float, sample>", !"air.arg_name", !"t"}

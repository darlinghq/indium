; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for:
;
;   kernel void sample_lod(device float* out [[buffer(0)]], texture2d<float> tex [[texture(0)]],
;                          sampler s [[sampler(0)]], uint id [[thread_position_in_grid]]) {
;       out[id] = tex.sample(s, float2(0.5), level(float(id) * 0.5)).x;
;   }
;
; The sample call has the form Metal's compiler gives it (see test/texturing). Assemble and wrap it
; as test/air-ops/README.md describes, with the function kernel:sample_lod.

source_filename = "sampler-lod"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @sample_lod(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) nocapture readonly %tex, ptr addrspace(2) nocapture readonly %s, i32 %id) local_unnamed_addr #0 {
entry:
  %idf = uitofp i32 %id to float
  %lod = fmul float %idf, 5.000000e-01
  %sample = tail call { <4 x float>, i8 } @air.sample_texture_2d.v4f32(ptr addrspace(1) nocapture readonly %tex, ptr addrspace(2) nocapture readonly %s, <2 x float> <float 5.000000e-01, float 5.000000e-01>, i1 true, <2 x i32> zeroinitializer, i1 true, float %lod, float 0.000000e+00, i32 0)
  %texel = extractvalue { <4 x float>, i8 } %sample, 0
  %r = extractelement <4 x float> %texel, i64 0
  %idx = zext i32 %id to i64
  %p = getelementptr inbounds float, ptr addrspace(1) %out, i64 %idx
  store float %r, ptr addrspace(1) %p, align 4
  ret void
}

declare { <4 x float>, i8 } @air.sample_texture_2d.v4f32(ptr addrspace(1) nocapture readonly, ptr addrspace(2) nocapture readonly, <2 x float>, i1, <2 x i32>, i1, float, float, i32)

attributes #0 = { norecurse nounwind }

!air.version = !{!0}
!air.language_version = !{!1}
!air.kernel = !{!2}

!0 = !{i32 2, i32 2, i32 0}
!1 = !{!"Metal", i32 2, i32 2, i32 0}
!2 = !{ptr @sample_lod, !3, !4}
!3 = !{}
!4 = !{!5, !6, !7, !8}
!5 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"out"}
!6 = !{i32 1, !"air.texture", !"air.location_index", i32 0, i32 1, !"air.sample", !"air.arg_type_name", !"texture2d<float, sample>", !"air.arg_name", !"tex"}
!7 = !{i32 2, !"air.sampler", !"air.location_index", i32 0, i32 1, !"air.arg_type_name", !"sampler", !"air.arg_name", !"s"}
!8 = !{i32 3, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}

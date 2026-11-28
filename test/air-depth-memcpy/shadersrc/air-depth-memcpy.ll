; Copyright (C) 2026 Lubos Dolezel
;
; Hand-written AIR for:
;
;   kernel void sample_depth(device float* out [[buffer(0)]], depth2d<float> tex [[texture(0)]],
;                            sampler s [[sampler(0)]], uint id [[thread_position_in_grid]]) {
;       out[id] = tex.sample(s, float2(float(id & 1) * 0.5 + 0.25, float(id >> 1) * 0.5 + 0.25), level(0));
;   }
;
;   kernel void copy16(device const float4* src [[buffer(0)]], device float4* dst [[buffer(1)]],
;                      uint id [[thread_position_in_grid]]) {
;       memcpy(&dst[id], &src[id], 16);    // a constant-length copy, which the compiler lowers to llvm.memcpy
;   }
;
; The depth sample has the form of the colour sample of test/sampler-lod, with a scalar result. Assemble and
; wrap it as test/air-ops/README.md describes, with the functions kernel:sample_depth kernel:copy16.

source_filename = "air-depth-memcpy"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @sample_depth(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) nocapture readonly %tex, ptr addrspace(2) nocapture readonly %s, i32 %id) local_unnamed_addr #0 {
entry:
  %xbit = and i32 %id, 1
  %ybit = lshr i32 %id, 1
  %xf = uitofp i32 %xbit to float
  %yf = uitofp i32 %ybit to float
  %xh = fmul float %xf, 5.000000e-01
  %yh = fmul float %yf, 5.000000e-01
  %x = fadd float %xh, 2.500000e-01
  %y = fadd float %yh, 2.500000e-01
  %c0 = insertelement <2 x float> undef, float %x, i32 0
  %coord = insertelement <2 x float> %c0, float %y, i32 1
  %d = tail call float @air.sample_depth_2d.f32(ptr addrspace(1) nocapture readonly %tex, ptr addrspace(2) nocapture readonly %s, <2 x float> %coord, i1 true, <2 x i32> zeroinitializer, i1 true, float 0.000000e+00, float 0.000000e+00, i32 0)
  %idx = zext i32 %id to i64
  %p = getelementptr inbounds float, ptr addrspace(1) %out, i64 %idx
  store float %d, ptr addrspace(1) %p, align 4
  ret void
}

define void @copy16(ptr addrspace(1) noalias nocapture readonly %src, ptr addrspace(1) noalias nocapture %dst, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %sp = getelementptr inbounds <4 x float>, ptr addrspace(1) %src, i64 %idx
  %dp = getelementptr inbounds <4 x float>, ptr addrspace(1) %dst, i64 %idx
  tail call void @llvm.memcpy.p1.p1.i64(ptr addrspace(1) align 16 %dp, ptr addrspace(1) align 16 %sp, i64 16, i1 false)
  ret void
}

declare float @air.sample_depth_2d.f32(ptr addrspace(1) nocapture readonly, ptr addrspace(2) nocapture readonly, <2 x float>, i1, <2 x i32>, i1, float, float, i32)
declare void @llvm.memcpy.p1.p1.i64(ptr addrspace(1) noalias nocapture writeonly, ptr addrspace(1) noalias nocapture readonly, i64, i1 immarg)

attributes #0 = { norecurse nounwind }

!air.version = !{!0}
!air.language_version = !{!1}
!air.kernel = !{!2, !9}

!0 = !{i32 2, i32 2, i32 0}
!1 = !{!"Metal", i32 2, i32 2, i32 0}
!2 = !{ptr @sample_depth, !3, !4}
!3 = !{}
!4 = !{!5, !6, !7, !8}
!5 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"out"}
!6 = !{i32 1, !"air.texture", !"air.location_index", i32 0, i32 1, !"air.sample", !"air.arg_type_name", !"depth2d<float, sample>", !"air.arg_name", !"tex"}
!7 = !{i32 2, !"air.sampler", !"air.location_index", i32 0, i32 1, !"air.arg_type_name", !"sampler", !"air.arg_name", !"s"}
!8 = !{i32 3, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}
!9 = !{ptr @copy16, !3, !10}
!10 = !{!11, !12, !13}
!11 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read", !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4", !"air.arg_name", !"src"}
!12 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read_write", !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4", !"air.arg_name", !"dst"}
!13 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}

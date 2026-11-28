; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the round-half-away test: air.round on float, half and a float vector. Thread id rounds
; in[id] and writes its results to out[id * n + k]; see round-half-away.cpp for what each slot holds. See
; README.md for how this becomes round-half-away.metallib.

source_filename = "round-half-away"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @round_f32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %x = bitcast i32 %va to float
  %r = call float @air.round.f32(float %x)
  %fr = call float @air.fast_round.f32(float %x)
  %v0 = insertelement <2 x float> poison, float %x, i32 0
  %nx = fneg float %x
  %v = insertelement <2 x float> %v0, float %nx, i32 1
  %rv = call <2 x float> @air.round.v2f32(<2 x float> %v)
  %l0 = extractelement <2 x float> %rv, i32 0
  %l1 = extractelement <2 x float> %rv, i32 1
  %res0 = bitcast float %r to i32
  %res1 = bitcast float %fr to i32
  %res2 = bitcast float %l0 to i32
  %res3 = bitcast float %l1 to i32
  %base = mul i32 %id, 4
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %res0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %res1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %res2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %res3, ptr addrspace(1) %p3, align 4
  ret void
}

define void @round_f16(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %t = trunc i32 %va to i16
  %x = bitcast i16 %t to half
  %r = call half @air.round.f16(half %x)
  %rb = bitcast half %r to i16
  %res0 = zext i16 %rb to i32
  %base = mul i32 %id, 1
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %res0, ptr addrspace(1) %p0, align 4
  ret void
}

declare float @air.round.f32(float)
declare float @air.fast_round.f32(float)
declare <2 x float> @air.round.v2f32(<2 x float>)
declare half @air.round.f16(half)

attributes #0 = { nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!10, !11}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 2, i32 0}
!2 = !{!"Metal", i32 2, i32 2, i32 0}
!3 = !{}
!4 = !{!5, !6, !7}
!5 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}
!6 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"in"}
!7 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}
!10 = !{ptr @round_f32, !3, !4}
!11 = !{ptr @round_f16, !3, !4}

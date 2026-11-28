; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the funnel-shifts test: llvm.fshl and llvm.fshr on 8, 16, 32 and 64-bit integers and a
; vector. Thread id shifts in[id] and in[(id + 8) % 16] by in[(id + 4) % 16] and writes its results to
; out[id * n + k]; see funnel-shifts.cpp for what each slot holds. See README.md for how this becomes
; funnel-shifts.metallib.

source_filename = "funnel-shifts"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @funnel_i8(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %k0 = add i32 %id, 4
  %k = and i32 %k0, 15
  %kdx = zext i32 %k to i64
  %pc = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %kdx
  %vc = load i32, ptr addrspace(1) %pc, align 4
  %a = trunc i32 %va to i8
  %b = trunc i32 %vb to i8
  %c = trunc i32 %vc to i8
  %fshl = call i8 @llvm.fshl.i8(i8 %a, i8 %b, i8 %c)
  %fshlx = zext i8 %fshl to i32
  %fshr = call i8 @llvm.fshr.i8(i8 %a, i8 %b, i8 %c)
  %fshrx = zext i8 %fshr to i32
  %base = mul i32 %id, 2
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %fshlx, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %fshrx, ptr addrspace(1) %p1, align 4
  ret void
}

define void @funnel_i16(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %k0 = add i32 %id, 4
  %k = and i32 %k0, 15
  %kdx = zext i32 %k to i64
  %pc = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %kdx
  %vc = load i32, ptr addrspace(1) %pc, align 4
  %a = trunc i32 %va to i16
  %b = trunc i32 %vb to i16
  %c = trunc i32 %vc to i16
  %fshl = call i16 @llvm.fshl.i16(i16 %a, i16 %b, i16 %c)
  %fshlx = zext i16 %fshl to i32
  %fshr = call i16 @llvm.fshr.i16(i16 %a, i16 %b, i16 %c)
  %fshrx = zext i16 %fshr to i32
  %base = mul i32 %id, 2
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %fshlx, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %fshrx, ptr addrspace(1) %p1, align 4
  ret void
}

define void @funnel_i32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %k0 = add i32 %id, 4
  %k = and i32 %k0, 15
  %kdx = zext i32 %k to i64
  %pc = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %kdx
  %vc = load i32, ptr addrspace(1) %pc, align 4
  %a = add i32 %va, 0
  %b = add i32 %vb, 0
  %c = add i32 %vc, 0
  %fshl = call i32 @llvm.fshl.i32(i32 %a, i32 %b, i32 %c)
  %fshr = call i32 @llvm.fshr.i32(i32 %a, i32 %b, i32 %c)
  %base = mul i32 %id, 2
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %fshl, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %fshr, ptr addrspace(1) %p1, align 4
  ret void
}

define void @funnel_i64(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %k0 = add i32 %id, 4
  %k = and i32 %k0, 15
  %kdx = zext i32 %k to i64
  %pc = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %kdx
  %vc = load i32, ptr addrspace(1) %pc, align 4
  %ah0 = zext i32 %va to i64
  %ah = shl i64 %ah0, 32
  %al = zext i32 %vb to i64
  %a = or i64 %ah, %al
  %bh0 = zext i32 %vb to i64
  %bh = shl i64 %bh0, 32
  %bl = zext i32 %vc to i64
  %b = or i64 %bh, %bl
  %ch0 = zext i32 %vc to i64
  %ch = shl i64 %ch0, 32
  %cl = zext i32 %va to i64
  %c = or i64 %ch, %cl
  %fshl = call i64 @llvm.fshl.i64(i64 %a, i64 %b, i64 %c)
  %fshllo = trunc i64 %fshl to i32
  %fshls = lshr i64 %fshl, 32
  %fshlhi = trunc i64 %fshls to i32
  %fshr = call i64 @llvm.fshr.i64(i64 %a, i64 %b, i64 %c)
  %fshrlo = trunc i64 %fshr to i32
  %fshrs = lshr i64 %fshr, 32
  %fshrhi = trunc i64 %fshrs to i32
  %base = mul i32 %id, 4
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %fshllo, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %fshlhi, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %fshrlo, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %fshrhi, ptr addrspace(1) %p3, align 4
  ret void
}

define void @funnel_v2i32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %k0 = add i32 %id, 4
  %k = and i32 %k0, 15
  %kdx = zext i32 %k to i64
  %pc = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %kdx
  %vc = load i32, ptr addrspace(1) %pc, align 4
  %a0 = insertelement <2 x i32> poison, i32 %va, i32 0
  %a = insertelement <2 x i32> %a0, i32 %vb, i32 1
  %b0 = insertelement <2 x i32> poison, i32 %vb, i32 0
  %b = insertelement <2 x i32> %b0, i32 %vc, i32 1
  %c0 = insertelement <2 x i32> poison, i32 %vc, i32 0
  %c = insertelement <2 x i32> %c0, i32 %va, i32 1
  %fshl = call <2 x i32> @llvm.fshl.v2i32(<2 x i32> %a, <2 x i32> %b, <2 x i32> %c)
  %fshl0 = extractelement <2 x i32> %fshl, i32 0
  %fshl1 = extractelement <2 x i32> %fshl, i32 1
  %fshr = call <2 x i32> @llvm.fshr.v2i32(<2 x i32> %a, <2 x i32> %b, <2 x i32> %c)
  %fshr0 = extractelement <2 x i32> %fshr, i32 0
  %fshr1 = extractelement <2 x i32> %fshr, i32 1
  %base = mul i32 %id, 4
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %fshl0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %fshl1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %fshr0, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %fshr1, ptr addrspace(1) %p3, align 4
  ret void
}

declare i8 @llvm.fshl.i8(i8, i8, i8)
declare i8 @llvm.fshr.i8(i8, i8, i8)
declare i16 @llvm.fshl.i16(i16, i16, i16)
declare i16 @llvm.fshr.i16(i16, i16, i16)
declare i32 @llvm.fshl.i32(i32, i32, i32)
declare i32 @llvm.fshr.i32(i32, i32, i32)
declare i64 @llvm.fshl.i64(i64, i64, i64)
declare i64 @llvm.fshr.i64(i64, i64, i64)
declare <2 x i32> @llvm.fshl.v2i32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @llvm.fshr.v2i32(<2 x i32>, <2 x i32>, <2 x i32>)

attributes #0 = { nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!10, !11, !12, !13, !14}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 2, i32 0}
!2 = !{!"Metal", i32 2, i32 2, i32 0}
!3 = !{}
!4 = !{!5, !6, !7}
!5 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}
!6 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"in"}
!7 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}
!10 = !{ptr @funnel_i8, !3, !4}
!11 = !{ptr @funnel_i16, !3, !4}
!12 = !{ptr @funnel_i32, !3, !4}
!13 = !{ptr @funnel_i64, !3, !4}
!14 = !{ptr @funnel_v2i32, !3, !4}

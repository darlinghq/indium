; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the bool-conversions test: conversions of i1, which Iridium translates to SPIR-V
; booleans, to wider integers and to floating point. Each kernel reads in[id] and writes its results to out[id * n + k]; see
; bool-conversions.cpp for what each slot holds. See README.md for how this becomes
; bool-conversions.metallib.

source_filename = "bool-conversions"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

; sign and zero extension of a boolean, scalar and vector
define void @extend_bool(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pv = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %v = load i32, ptr addrspace(1) %pv, align 4
  %neg = icmp slt i32 %v, 0
  %r0 = sext i1 %neg to i32
  %s64 = sext i1 %neg to i64
  %h64 = lshr i64 %s64, 32
  %r1 = trunc i64 %h64 to i32
  %s16 = sext i1 %neg to i16
  %r2 = zext i16 %s16 to i32
  %s8 = sext i1 %neg to i8
  %r3 = zext i8 %s8 to i32
  %v2 = shl i32 %v, 1
  %va = insertelement <2 x i32> undef, i32 %v, i64 0
  %vb = insertelement <2 x i32> %va, i32 %v2, i64 1
  %vneg = icmp slt <2 x i32> %vb, zeroinitializer
  %vs = sext <2 x i1> %vneg to <2 x i32>
  %r4 = extractelement <2 x i32> %vs, i64 0
  %r5 = extractelement <2 x i32> %vs, i64 1
  %r6 = zext i1 %neg to i32
  %base = mul i32 %id, 7
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %r0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %r1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %r2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %r3, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %r4, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %r5, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %r6, ptr addrspace(1) %p6, align 4
  ret void
}

; a boolean converted to floating point: 1.0 or 0.0 unsigned, -1.0 or 0.0 signed, stored as its bits
define void @bool_to_float(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pv = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %v = load i32, ptr addrspace(1) %pv, align 4
  %neg = icmp slt i32 %v, 0
  %fu = uitofp i1 %neg to float
  %r0 = bitcast float %fu to i32
  %fs = sitofp i1 %neg to float
  %r1 = bitcast float %fs to i32
  %hu = uitofp i1 %neg to half
  %hub = bitcast half %hu to i16
  %r2 = zext i16 %hub to i32
  %hs = sitofp i1 %neg to half
  %hsb = bitcast half %hs to i16
  %r3 = zext i16 %hsb to i32
  %v2 = shl i32 %v, 1
  %va = insertelement <2 x i32> undef, i32 %v, i64 0
  %vb = insertelement <2 x i32> %va, i32 %v2, i64 1
  %vneg = icmp slt <2 x i32> %vb, zeroinitializer
  %vu = uitofp <2 x i1> %vneg to <2 x float>
  %fu1 = extractelement <2 x float> %vu, i64 1
  %r4 = bitcast float %fu1 to i32
  %vs = sitofp <2 x i1> %vneg to <2 x float>
  %fs1 = extractelement <2 x float> %vs, i64 1
  %r5 = bitcast float %fs1 to i32
  %base = mul i32 %id, 6
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %r0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %r1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %r2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %r3, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %r4, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %r5, ptr addrspace(1) %p5, align 4
  ret void
}

attributes #0 = { nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!3, !8}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 2, i32 0}
!2 = !{!"Metal", i32 2, i32 2, i32 0}

!3 = !{ptr @extend_bool, !4, !5}
!4 = !{}
!5 = !{!6, !7, !10}
!6 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}
!7 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"in"}
!10 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}
!8 = !{ptr @bool_to_float, !4, !5}

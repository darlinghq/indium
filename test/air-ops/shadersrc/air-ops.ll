; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the kernels in air-ops.metal, in the form Metal's compiler gives such code:
; opaque pointers, buffers in address space 1, program-scope constants in address space 2, and
; Metal library functions called as air.* intrinsics. See README.md for how this becomes
; air-ops.metallib.

source_filename = "air-ops"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

@table = internal addrspace(2) constant [8 x float] [float 1.0, float 2.0, float 4.0, float 8.0, float 16.0, float 32.0, float 64.0, float 128.0], align 4

; out[id * 18 + k] for k = 0..17, see air-ops.cpp for what each slot holds
define void @int_ops(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %a, ptr addrspace(1) noalias nocapture readonly %b, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %a, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %pb = getelementptr inbounds i32, ptr addrspace(1) %b, i64 %idx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %base = mul i32 %id, 18

  %r0 = udiv i32 %va, %vb
  %r1 = urem i32 %va, %vb
  %r2 = sdiv i32 %va, %vb
  %r3 = srem i32 %va, %vb
  %sh = and i32 %vb, 7
  %r4 = shl i32 %va, %sh
  %r5 = lshr i32 %va, %sh
  %r6 = ashr i32 %va, %sh
  %r7 = and i32 %va, %vb
  %r8 = or i32 %va, %vb
  %r9 = xor i32 %va, %vb
  %lt = icmp slt i32 %va, %vb
  %r10 = zext i1 %lt to i32
  %ult = icmp ult i32 %va, %vb
  %r11 = select i1 %ult, i32 %va, i32 %vb
  %r12 = call i32 @air.min.s.i32(i32 %va, i32 %vb)
  %r13 = call i32 @air.max.u.i32(i32 %va, i32 %vb)
  %r14 = call i32 @air.abs.s.i32(i32 %va)
  %t16 = trunc i32 %va to i16
  %r15 = sext i16 %t16 to i32
  %r16 = call i32 @air.clamp.s.i32(i32 %va, i32 -5, i32 5)
  %t8 = trunc i32 %va to i8
  %r17 = zext i8 %t8 to i32

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
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %r7, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x8
  store i32 %r8, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x9
  store i32 %r9, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %x10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x10
  store i32 %r10, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %x11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x11
  store i32 %r11, ptr addrspace(1) %p11, align 4
  %i12 = add i32 %base, 12
  %x12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x12
  store i32 %r12, ptr addrspace(1) %p12, align 4
  %i13 = add i32 %base, 13
  %x13 = zext i32 %i13 to i64
  %p13 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x13
  store i32 %r13, ptr addrspace(1) %p13, align 4
  %i14 = add i32 %base, 14
  %x14 = zext i32 %i14 to i64
  %p14 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x14
  store i32 %r14, ptr addrspace(1) %p14, align 4
  %i15 = add i32 %base, 15
  %x15 = zext i32 %i15 to i64
  %p15 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x15
  store i32 %r15, ptr addrspace(1) %p15, align 4
  %i16 = add i32 %base, 16
  %x16 = zext i32 %i16 to i64
  %p16 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x16
  store i32 %r16, ptr addrspace(1) %p16, align 4
  %i17 = add i32 %base, 17
  %x17 = zext i32 %i17 to i64
  %p17 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x17
  store i32 %r17, ptr addrspace(1) %p17, align 4
  ret void
}

; out[id * 16 + k] for k = 0..15
define void @float_ops(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %x, ptr addrspace(1) noalias nocapture readonly %y, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %px = getelementptr inbounds float, ptr addrspace(1) %x, i64 %idx
  %vx = load float, ptr addrspace(1) %px, align 4
  %py = getelementptr inbounds float, ptr addrspace(1) %y, i64 %idx
  %vy = load float, ptr addrspace(1) %py, align 4
  %base = mul i32 %id, 16

  %r0 = fneg float %vx
  %r1 = frem float %vx, %vy
  %ax = call float @air.fabs.f32(float %vx)
  %r2 = call float @air.sqrt.f32(float %ax)
  %r3 = call float @air.floor.f32(float %vx)
  %r4 = call float @air.fmin.f32(float %vx, float %vy)
  %r5 = call float @air.fma.f32(float %vx, float %vy, float 1.0)
  %si = fptosi float %vx to i32
  %r6 = sitofp i32 %si to float
  %ui = fptoui float %ax to i32
  %r7 = uitofp i32 %ui to float
  %ge = fcmp oge float %vx, %vy
  %r8 = select i1 %ge, float 1.0, float 0.0
  %le = fcmp ole float %vx, %vy
  %r9 = select i1 %le, float 1.0, float 0.0
  %ne = fcmp une float %vx, %vy
  %r10 = select i1 %ne, float 1.0, float 0.0
  %eq = fcmp oeq float %vx, %vy
  %r11 = select i1 %eq, float 1.0, float 0.0
  %si2 = mul i32 %si, 2
  %r12 = call float @air.convert.f.f32.s.i32(i32 %si2)
  %cu = call i32 @air.convert.u.i32.f.f32(float %ax)
  %r13 = uitofp i32 %cu to float
  %r14 = call float @air.clamp.f32(float %vx, float -1.0, float 1.0)
  %r15 = call float @air.fract.f32(float %ax)
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x0
  store float %r0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x1
  store float %r1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x2
  store float %r2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x3
  store float %r3, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x4
  store float %r4, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x5
  store float %r5, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x6
  store float %r6, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x7
  store float %r7, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x8
  store float %r8, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x9
  store float %r9, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %x10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x10
  store float %r10, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %x11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x11
  store float %r11, ptr addrspace(1) %p11, align 4
  %i12 = add i32 %base, 12
  %x12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x12
  store float %r12, ptr addrspace(1) %p12, align 4
  %i13 = add i32 %base, 13
  %x13 = zext i32 %i13 to i64
  %p13 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x13
  store float %r13, ptr addrspace(1) %p13, align 4
  %i14 = add i32 %base, 14
  %x14 = zext i32 %i14 to i64
  %p14 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x14
  store float %r14, ptr addrspace(1) %p14, align 4
  %i15 = add i32 %base, 15
  %x15 = zext i32 %i15 to i64
  %p15 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x15
  store float %r15, ptr addrspace(1) %p15, align 4
  ret void
}

; out[id] = table[id], with the table a program-scope constant array
define void @table_lookup(ptr addrspace(1) noalias nocapture %out, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pt = getelementptr inbounds [8 x float], ptr addrspace(2) @table, i64 0, i64 %idx
  %v = load float, ptr addrspace(2) %pt, align 4
  %po = getelementptr inbounds float, ptr addrspace(1) %out, i64 %idx
  store float %v, ptr addrspace(1) %po, align 4
  ret void
}

declare i32 @air.min.s.i32(i32, i32)
declare i32 @air.max.u.i32(i32, i32)
declare i32 @air.abs.s.i32(i32)
declare i32 @air.clamp.s.i32(i32, i32, i32)
declare float @air.fabs.f32(float)
declare float @air.sqrt.f32(float)
declare float @air.floor.f32(float)
declare float @air.fmin.f32(float, float)
declare float @air.fma.f32(float, float, float)
declare float @air.convert.f.f32.s.i32(i32)
declare i32 @air.convert.u.i32.f.f32(float)
declare float @air.clamp.f32(float, float, float)
declare float @air.fract.f32(float)

attributes #0 = { norecurse nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!3, !10, !15}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 2, i32 0}
!2 = !{!"Metal", i32 2, i32 2, i32 0}

!3 = !{ptr @int_ops, !4, !5}
!4 = !{}
!5 = !{!6, !7, !8, !9}
!6 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}
!7 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"a"}
!8 = !{i32 2, !"air.buffer", !"air.location_index", i32 2, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"b"}
!9 = !{i32 3, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}

!10 = !{ptr @float_ops, !4, !11}
!11 = !{!12, !13, !14, !9}
!12 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"out"}
!13 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"x"}
!14 = !{i32 2, !"air.buffer", !"air.location_index", i32 2, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"y"}

!15 = !{ptr @table_lookup, !4, !16}
!16 = !{!12, !17}
!17 = !{i32 1, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}

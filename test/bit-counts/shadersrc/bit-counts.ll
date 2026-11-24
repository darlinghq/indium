; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the bit-counts test: the MSL popcount, clz, ctz and reverse_bits functions on 8, 16
; and 64-bit integers, called as air.* intrinsics. Each kernel reads in[id] and writes its results to
; out[id * n + k]; see bit-counts.cpp for what each slot holds. See README.md for how this becomes
; bit-counts.metallib.

source_filename = "bit-counts"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

; the low byte of in[id]
define void @counts_i8(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pv = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %v = load i32, ptr addrspace(1) %pv, align 4
  %x = trunc i32 %v to i8
  %c0 = call i8 @air.popcount.i8(i8 %x)
  %r0 = zext i8 %c0 to i32
  %c1 = call i8 @air.clz.i8(i8 %x)
  %r1 = zext i8 %c1 to i32
  %c2 = call i8 @air.ctz.i8(i8 %x)
  %r2 = zext i8 %c2 to i32
  %c3 = call i8 @air.reverse_bits.i8(i8 %x)
  %r3 = zext i8 %c3 to i32
  %base = mul i32 %id, 4
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
  ret void
}

; the low half of in[id], and the vector of both halves
define void @counts_i16(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pv = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %v = load i32, ptr addrspace(1) %pv, align 4
  %x = trunc i32 %v to i16
  %c0 = call i16 @air.popcount.i16(i16 %x)
  %r0 = zext i16 %c0 to i32
  %c1 = call i16 @air.clz.i16(i16 %x)
  %r1 = zext i16 %c1 to i32
  %c2 = call i16 @air.ctz.i16(i16 %x)
  %r2 = zext i16 %c2 to i32
  %c3 = call i16 @air.reverse_bits.i16(i16 %x)
  %r3 = zext i16 %c3 to i32
  %vh = lshr i32 %v, 16
  %h = trunc i32 %vh to i16
  %va = insertelement <2 x i16> undef, i16 %x, i64 0
  %vb = insertelement <2 x i16> %va, i16 %h, i64 1
  %vc = call <2 x i16> @air.clz.v2i16(<2 x i16> %vb)
  %vc0 = extractelement <2 x i16> %vc, i64 0
  %r4 = zext i16 %vc0 to i32
  %vc1 = extractelement <2 x i16> %vc, i64 1
  %r5 = zext i16 %vc1 to i32
  %vt = call <2 x i16> @air.ctz.v2i16(<2 x i16> %vb)
  %vt1 = extractelement <2 x i16> %vt, i64 1
  %r6 = zext i16 %vt1 to i32
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

; in[id] in the low half (a), the high half (b) and both halves (c) of a 64-bit integer, and d with in[id]
; in the high half and in[id] ^ 0x0F0F0F0F in the low half
define void @counts_i64(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pv = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %v = load i32, ptr addrspace(1) %pv, align 4
  %a = zext i32 %v to i64
  %b = shl i64 %a, 32
  %c = or i64 %a, %b
  %pa = call i64 @air.popcount.i64(i64 %a)
  %r0 = trunc i64 %pa to i32
  %pb = call i64 @air.popcount.i64(i64 %b)
  %r1 = trunc i64 %pb to i32
  %pc = call i64 @air.popcount.i64(i64 %c)
  %r2 = trunc i64 %pc to i32
  %la = call i64 @air.clz.i64(i64 %a)
  %r3 = trunc i64 %la to i32
  %lb = call i64 @air.clz.i64(i64 %b)
  %r4 = trunc i64 %lb to i32
  %lc = call i64 @air.clz.i64(i64 %c)
  %r5 = trunc i64 %lc to i32
  %ta = call i64 @air.ctz.i64(i64 %a)
  %r6 = trunc i64 %ta to i32
  %tb = call i64 @air.ctz.i64(i64 %b)
  %r7 = trunc i64 %tb to i32
  %tc = call i64 @air.ctz.i64(i64 %c)
  %r8 = trunc i64 %tc to i32
  %vx = xor i32 %v, 252645135
  %zx = zext i32 %vx to i64
  %d = or i64 %b, %zx
  %ra = call i64 @air.reverse_bits.i64(i64 %d)
  %r9 = trunc i64 %ra to i32
  %rah = lshr i64 %ra, 32
  %r10 = trunc i64 %rah to i32
  %base = mul i32 %id, 11
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
  ret void
}

declare i8 @air.popcount.i8(i8)
declare i8 @air.clz.i8(i8)
declare i8 @air.ctz.i8(i8)
declare i8 @air.reverse_bits.i8(i8)
declare i16 @air.popcount.i16(i16)
declare i16 @air.clz.i16(i16)
declare i16 @air.ctz.i16(i16)
declare i16 @air.reverse_bits.i16(i16)
declare <2 x i16> @air.clz.v2i16(<2 x i16>)
declare <2 x i16> @air.ctz.v2i16(<2 x i16>)
declare i64 @air.popcount.i64(i64)
declare i64 @air.clz.i64(i64)
declare i64 @air.ctz.i64(i64)
declare i64 @air.reverse_bits.i64(i64)

attributes #0 = { nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!3, !8, !9}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 2, i32 0}
!2 = !{!"Metal", i32 2, i32 2, i32 0}

!3 = !{ptr @counts_i8, !4, !5}
!4 = !{}
!5 = !{!6, !7, !10}
!6 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}
!7 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"in"}
!10 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}
!8 = !{ptr @counts_i16, !4, !5}
!9 = !{ptr @counts_i64, !4, !5}

; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the simd-narrow-shuffles test: the simdgroup broadcasts and shuffles of 8 and 16-bit
; integers and halfs. One threadgroup runs each kernel; thread t shuffles its own value of t (see
; simd-narrow-shuffles.cpp) and writes the results to out[t * 8 + k]. See README.md for how this becomes
; simd-narrow-shuffles.metallib.

source_filename = "simd-narrow-shuffles"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @shuffle_s_i8(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %m = mul i32 %t, 37
  %a = add i32 %m, 133
  %v = trunc i32 %a to i8
  %l1 = add i32 %lane, 1
  %wm = sub i32 %width, 1
  %l2 = and i32 %l1, %wm
  %src = trunc i32 %l2 to i16
  %last = trunc i32 %wm to i16
  %r0 = call i8 @air.simd_shuffle.s.i8(i8 %v, i16 %src) #1
  %o0 = zext i8 %r0 to i32
  %r1 = call i8 @air.simd_shuffle_xor.s.i8(i8 %v, i16 1) #1
  %o1 = zext i8 %r1 to i32
  %r2 = call i8 @air.simd_shuffle_down.s.i8(i8 %v, i16 1) #1
  %o2 = zext i8 %r2 to i32
  %r3 = call i8 @air.simd_shuffle_up.s.i8(i8 %v, i16 1) #1
  %o3 = zext i8 %r3 to i32
  %r4 = call i8 @air.simd_broadcast.s.i8(i8 %v, i16 3) #1
  %o4 = zext i8 %r4 to i32
  %r5 = call i8 @air.simd_broadcast.s.i8(i8 %v, i16 %last) #1
  %o5 = zext i8 %r5 to i32
  %r6 = call i8 @air.simd_broadcast_first.s.i8(i8 %v) #1
  %o6 = zext i8 %r6 to i32
  %base = mul i32 %t, 8
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %o0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %o1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %o2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %o3, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %o4, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %o5, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %o6, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %width, ptr addrspace(1) %p7, align 4
  ret void
}

define void @shuffle_u_i8(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %m = mul i32 %t, 37
  %a = add i32 %m, 133
  %v = trunc i32 %a to i8
  %l1 = add i32 %lane, 1
  %wm = sub i32 %width, 1
  %l2 = and i32 %l1, %wm
  %src = trunc i32 %l2 to i16
  %last = trunc i32 %wm to i16
  %r0 = call i8 @air.simd_shuffle.u.i8(i8 %v, i16 %src) #1
  %o0 = zext i8 %r0 to i32
  %r1 = call i8 @air.simd_shuffle_xor.u.i8(i8 %v, i16 1) #1
  %o1 = zext i8 %r1 to i32
  %r2 = call i8 @air.simd_shuffle_down.u.i8(i8 %v, i16 1) #1
  %o2 = zext i8 %r2 to i32
  %r3 = call i8 @air.simd_shuffle_up.u.i8(i8 %v, i16 1) #1
  %o3 = zext i8 %r3 to i32
  %r4 = call i8 @air.simd_broadcast.u.i8(i8 %v, i16 3) #1
  %o4 = zext i8 %r4 to i32
  %r5 = call i8 @air.simd_broadcast.u.i8(i8 %v, i16 %last) #1
  %o5 = zext i8 %r5 to i32
  %r6 = call i8 @air.simd_broadcast_first.u.i8(i8 %v) #1
  %o6 = zext i8 %r6 to i32
  %base = mul i32 %t, 8
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %o0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %o1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %o2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %o3, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %o4, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %o5, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %o6, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %width, ptr addrspace(1) %p7, align 4
  ret void
}

define void @shuffle_s_i16(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %m = mul i32 %t, 1031
  %a = add i32 %m, 32771
  %v = trunc i32 %a to i16
  %l1 = add i32 %lane, 1
  %wm = sub i32 %width, 1
  %l2 = and i32 %l1, %wm
  %src = trunc i32 %l2 to i16
  %last = trunc i32 %wm to i16
  %r0 = call i16 @air.simd_shuffle.s.i16(i16 %v, i16 %src) #1
  %o0 = zext i16 %r0 to i32
  %r1 = call i16 @air.simd_shuffle_xor.s.i16(i16 %v, i16 1) #1
  %o1 = zext i16 %r1 to i32
  %r2 = call i16 @air.simd_shuffle_down.s.i16(i16 %v, i16 1) #1
  %o2 = zext i16 %r2 to i32
  %r3 = call i16 @air.simd_shuffle_up.s.i16(i16 %v, i16 1) #1
  %o3 = zext i16 %r3 to i32
  %r4 = call i16 @air.simd_broadcast.s.i16(i16 %v, i16 3) #1
  %o4 = zext i16 %r4 to i32
  %r5 = call i16 @air.simd_broadcast.s.i16(i16 %v, i16 %last) #1
  %o5 = zext i16 %r5 to i32
  %r6 = call i16 @air.simd_broadcast_first.s.i16(i16 %v) #1
  %o6 = zext i16 %r6 to i32
  %base = mul i32 %t, 8
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %o0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %o1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %o2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %o3, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %o4, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %o5, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %o6, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %width, ptr addrspace(1) %p7, align 4
  ret void
}

define void @shuffle_u_i16(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %m = mul i32 %t, 1031
  %a = add i32 %m, 32771
  %v = trunc i32 %a to i16
  %l1 = add i32 %lane, 1
  %wm = sub i32 %width, 1
  %l2 = and i32 %l1, %wm
  %src = trunc i32 %l2 to i16
  %last = trunc i32 %wm to i16
  %r0 = call i16 @air.simd_shuffle.u.i16(i16 %v, i16 %src) #1
  %o0 = zext i16 %r0 to i32
  %r1 = call i16 @air.simd_shuffle_xor.u.i16(i16 %v, i16 1) #1
  %o1 = zext i16 %r1 to i32
  %r2 = call i16 @air.simd_shuffle_down.u.i16(i16 %v, i16 1) #1
  %o2 = zext i16 %r2 to i32
  %r3 = call i16 @air.simd_shuffle_up.u.i16(i16 %v, i16 1) #1
  %o3 = zext i16 %r3 to i32
  %r4 = call i16 @air.simd_broadcast.u.i16(i16 %v, i16 3) #1
  %o4 = zext i16 %r4 to i32
  %r5 = call i16 @air.simd_broadcast.u.i16(i16 %v, i16 %last) #1
  %o5 = zext i16 %r5 to i32
  %r6 = call i16 @air.simd_broadcast_first.u.i16(i16 %v) #1
  %o6 = zext i16 %r6 to i32
  %base = mul i32 %t, 8
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %o0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %o1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %o2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %o3, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %o4, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %o5, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %o6, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %width, ptr addrspace(1) %p7, align 4
  ret void
}

define void @shuffle_f16(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %f = uitofp i32 %t to float
  %a = fadd float %f, 5.000000e-01
  %v = fptrunc float %a to half
  %l1 = add i32 %lane, 1
  %wm = sub i32 %width, 1
  %l2 = and i32 %l1, %wm
  %src = trunc i32 %l2 to i16
  %last = trunc i32 %wm to i16
  %r0 = call half @air.simd_shuffle.f16(half %v, i16 %src) #1
  %e0 = fpext half %r0 to float
  %o0 = bitcast float %e0 to i32
  %r1 = call half @air.simd_shuffle_xor.f16(half %v, i16 1) #1
  %e1 = fpext half %r1 to float
  %o1 = bitcast float %e1 to i32
  %r2 = call half @air.simd_shuffle_down.f16(half %v, i16 1) #1
  %e2 = fpext half %r2 to float
  %o2 = bitcast float %e2 to i32
  %r3 = call half @air.simd_shuffle_up.f16(half %v, i16 1) #1
  %e3 = fpext half %r3 to float
  %o3 = bitcast float %e3 to i32
  %r4 = call half @air.simd_broadcast.f16(half %v, i16 3) #1
  %e4 = fpext half %r4 to float
  %o4 = bitcast float %e4 to i32
  %r5 = call half @air.simd_broadcast.f16(half %v, i16 %last) #1
  %e5 = fpext half %r5 to float
  %o5 = bitcast float %e5 to i32
  %r6 = call half @air.simd_broadcast_first.f16(half %v) #1
  %e6 = fpext half %r6 to float
  %o6 = bitcast float %e6 to i32
  %base = mul i32 %t, 8
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %o0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %o1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %o2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %o3, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %o4, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %o5, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %o6, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %width, ptr addrspace(1) %p7, align 4
  ret void
}

declare i8 @air.simd_shuffle.s.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.simd_shuffle_xor.s.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.simd_shuffle_down.s.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.simd_shuffle_up.s.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.simd_broadcast.s.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.simd_broadcast_first.s.i8(i8) local_unnamed_addr #1
declare i8 @air.simd_shuffle.u.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.simd_shuffle_xor.u.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.simd_shuffle_down.u.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.simd_shuffle_up.u.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.simd_broadcast.u.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.simd_broadcast_first.u.i8(i8) local_unnamed_addr #1
declare i16 @air.simd_shuffle.s.i16(i16, i16) local_unnamed_addr #1
declare i16 @air.simd_shuffle_xor.s.i16(i16, i16) local_unnamed_addr #1
declare i16 @air.simd_shuffle_down.s.i16(i16, i16) local_unnamed_addr #1
declare i16 @air.simd_shuffle_up.s.i16(i16, i16) local_unnamed_addr #1
declare i16 @air.simd_broadcast.s.i16(i16, i16) local_unnamed_addr #1
declare i16 @air.simd_broadcast_first.s.i16(i16) local_unnamed_addr #1
declare i16 @air.simd_shuffle.u.i16(i16, i16) local_unnamed_addr #1
declare i16 @air.simd_shuffle_xor.u.i16(i16, i16) local_unnamed_addr #1
declare i16 @air.simd_shuffle_down.u.i16(i16, i16) local_unnamed_addr #1
declare i16 @air.simd_shuffle_up.u.i16(i16, i16) local_unnamed_addr #1
declare i16 @air.simd_broadcast.u.i16(i16, i16) local_unnamed_addr #1
declare i16 @air.simd_broadcast_first.u.i16(i16) local_unnamed_addr #1
declare half @air.simd_shuffle.f16(half, i16) local_unnamed_addr #1
declare half @air.simd_shuffle_xor.f16(half, i16) local_unnamed_addr #1
declare half @air.simd_shuffle_down.f16(half, i16) local_unnamed_addr #1
declare half @air.simd_shuffle_up.f16(half, i16) local_unnamed_addr #1
declare half @air.simd_broadcast.f16(half, i16) local_unnamed_addr #1
declare half @air.simd_broadcast_first.f16(half) local_unnamed_addr #1

attributes #0 = { nounwind }
attributes #1 = { convergent nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!10, !11, !12, !13, !14}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 4, i32 0}
!2 = !{!"Metal", i32 2, i32 4, i32 0}
!3 = !{}
!4 = !{!5, !6, !7, !8}
!5 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!6 = !{i32 1, !"air.thread_position_in_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"t"}
!7 = !{i32 2, !"air.thread_index_in_simdgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"lane"}
!8 = !{i32 3, !"air.threads_per_simdgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"width"}
!10 = !{ptr @shuffle_s_i8, !3, !4}
!11 = !{ptr @shuffle_u_i8, !3, !4}
!12 = !{ptr @shuffle_s_i16, !3, !4}
!13 = !{ptr @shuffle_u_i16, !3, !4}
!14 = !{ptr @shuffle_f16, !3, !4}


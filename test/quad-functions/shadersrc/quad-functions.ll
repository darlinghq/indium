; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the quad-functions test: the quad-group shuffles of 32-bit integers, floats, 8-bit
; integers and halfs, and quad_ballot, and the votes of simdgroup and quad-group masks. One threadgroup
; runs each kernel; thread t works on its own values of t (see quad-functions.cpp) and writes the results to
; out[t * n + k]. See README.md for how this becomes quad-functions.metallib.

source_filename = "quad-functions"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @quad_shuffles(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %qlane = and i32 %lane, 3
  %rev = sub i32 3, %qlane
  %rev16 = trunc i32 %rev to i16
  %next = add i32 %qlane, 1
  %next3 = and i32 %next, 3
  %next16 = trunc i32 %next3 to i16
  %m = mul i32 %t, 3
  %vi = add i32 %m, 100
  %fi = add i32 %m, 1000
  %v = add i32 %vi, 0
  %f = add i32 %fi, 0
  %r0 = call i32 @air.quad_shuffle.s.i32(i32 %v, i16 %rev16) #1
  %o0 = add i32 %r0, 0
  %r1 = call i32 @air.quad_shuffle.s.i32(i32 %v, i16 %next16) #1
  %o1 = add i32 %r1, 0
  %r2 = call i32 @air.quad_shuffle_xor.s.i32(i32 %v, i16 1) #1
  %o2 = add i32 %r2, 0
  %r3 = call i32 @air.quad_shuffle_xor.s.i32(i32 %v, i16 2) #1
  %o3 = add i32 %r3, 0
  %r4 = call i32 @air.quad_shuffle_down.s.i32(i32 %v, i16 1) #1
  %o4 = add i32 %r4, 0
  %r5 = call i32 @air.quad_shuffle_down.s.i32(i32 %v, i16 2) #1
  %o5 = add i32 %r5, 0
  %r6 = call i32 @air.quad_shuffle_up.s.i32(i32 %v, i16 1) #1
  %o6 = add i32 %r6, 0
  %r7 = call i32 @air.quad_shuffle_up.s.i32(i32 %v, i16 3) #1
  %o7 = add i32 %r7, 0
  %r8 = call i32 @air.quad_shuffle_and_fill_down.s.i32(i32 %v, i32 %f, i16 1, i16 2) #1
  %o8 = add i32 %r8, 0
  %r9 = call i32 @air.quad_shuffle_and_fill_down.s.i32(i32 %v, i32 %f, i16 2, i16 4) #1
  %o9 = add i32 %r9, 0
  %r10 = call i32 @air.quad_shuffle_and_fill_up.s.i32(i32 %v, i32 %f, i16 1, i16 2) #1
  %o10 = add i32 %r10, 0
  %r11 = call i32 @air.quad_shuffle_and_fill_up.s.i32(i32 %v, i32 %f, i16 3, i16 4) #1
  %o11 = add i32 %r11, 0
  %base = mul i32 %t, 13
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
  store i32 %o7, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x8
  store i32 %o8, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x9
  store i32 %o9, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %x10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x10
  store i32 %o10, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %x11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x11
  store i32 %o11, ptr addrspace(1) %p11, align 4
  %i12 = add i32 %base, 12
  %x12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x12
  store i32 %width, ptr addrspace(1) %p12, align 4
  ret void
}

define void @quad_shuffles_float(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %qlane = and i32 %lane, 3
  %rev = sub i32 3, %qlane
  %rev16 = trunc i32 %rev to i16
  %next = add i32 %qlane, 1
  %next3 = and i32 %next, 3
  %next16 = trunc i32 %next3 to i16
  %m = mul i32 %t, 3
  %vi = add i32 %m, 100
  %fi = add i32 %m, 1000
  %v = uitofp i32 %vi to float
  %f = uitofp i32 %fi to float
  %r0 = call float @air.quad_shuffle.f32(float %v, i16 %rev16) #1
  %o0 = fptoui float %r0 to i32
  %r1 = call float @air.quad_shuffle.f32(float %v, i16 %next16) #1
  %o1 = fptoui float %r1 to i32
  %r2 = call float @air.quad_shuffle_xor.f32(float %v, i16 1) #1
  %o2 = fptoui float %r2 to i32
  %r3 = call float @air.quad_shuffle_xor.f32(float %v, i16 2) #1
  %o3 = fptoui float %r3 to i32
  %r4 = call float @air.quad_shuffle_down.f32(float %v, i16 1) #1
  %o4 = fptoui float %r4 to i32
  %r5 = call float @air.quad_shuffle_down.f32(float %v, i16 2) #1
  %o5 = fptoui float %r5 to i32
  %r6 = call float @air.quad_shuffle_up.f32(float %v, i16 1) #1
  %o6 = fptoui float %r6 to i32
  %r7 = call float @air.quad_shuffle_up.f32(float %v, i16 3) #1
  %o7 = fptoui float %r7 to i32
  %r8 = call float @air.quad_shuffle_and_fill_down.f32(float %v, float %f, i16 1, i16 2) #1
  %o8 = fptoui float %r8 to i32
  %r9 = call float @air.quad_shuffle_and_fill_down.f32(float %v, float %f, i16 2, i16 4) #1
  %o9 = fptoui float %r9 to i32
  %r10 = call float @air.quad_shuffle_and_fill_up.f32(float %v, float %f, i16 1, i16 2) #1
  %o10 = fptoui float %r10 to i32
  %r11 = call float @air.quad_shuffle_and_fill_up.f32(float %v, float %f, i16 3, i16 4) #1
  %o11 = fptoui float %r11 to i32
  %base = mul i32 %t, 13
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
  store i32 %o7, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x8
  store i32 %o8, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x9
  store i32 %o9, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %x10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x10
  store i32 %o10, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %x11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x11
  store i32 %o11, ptr addrspace(1) %p11, align 4
  %i12 = add i32 %base, 12
  %x12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x12
  store i32 %width, ptr addrspace(1) %p12, align 4
  ret void
}

define void @quad_shuffles_char(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %qlane = and i32 %lane, 3
  %rev = sub i32 3, %qlane
  %rev16 = trunc i32 %rev to i16
  %next = add i32 %qlane, 1
  %next3 = and i32 %next, 3
  %next16 = trunc i32 %next3 to i16
  %m = mul i32 %t, 3
  %vi = add i32 %m, 100
  %fi = add i32 %m, 1000
  %v = trunc i32 %vi to i8
  %f = trunc i32 %fi to i8
  %r0 = call i8 @air.quad_shuffle.u.i8(i8 %v, i16 %rev16) #1
  %o0 = zext i8 %r0 to i32
  %r1 = call i8 @air.quad_shuffle.u.i8(i8 %v, i16 %next16) #1
  %o1 = zext i8 %r1 to i32
  %r2 = call i8 @air.quad_shuffle_xor.u.i8(i8 %v, i16 1) #1
  %o2 = zext i8 %r2 to i32
  %r3 = call i8 @air.quad_shuffle_xor.u.i8(i8 %v, i16 2) #1
  %o3 = zext i8 %r3 to i32
  %r4 = call i8 @air.quad_shuffle_down.u.i8(i8 %v, i16 1) #1
  %o4 = zext i8 %r4 to i32
  %r5 = call i8 @air.quad_shuffle_down.u.i8(i8 %v, i16 2) #1
  %o5 = zext i8 %r5 to i32
  %r6 = call i8 @air.quad_shuffle_up.u.i8(i8 %v, i16 1) #1
  %o6 = zext i8 %r6 to i32
  %r7 = call i8 @air.quad_shuffle_up.u.i8(i8 %v, i16 3) #1
  %o7 = zext i8 %r7 to i32
  %r8 = call i8 @air.quad_shuffle_and_fill_down.u.i8(i8 %v, i8 %f, i16 1, i16 2) #1
  %o8 = zext i8 %r8 to i32
  %r9 = call i8 @air.quad_shuffle_and_fill_down.u.i8(i8 %v, i8 %f, i16 2, i16 4) #1
  %o9 = zext i8 %r9 to i32
  %r10 = call i8 @air.quad_shuffle_and_fill_up.u.i8(i8 %v, i8 %f, i16 1, i16 2) #1
  %o10 = zext i8 %r10 to i32
  %r11 = call i8 @air.quad_shuffle_and_fill_up.u.i8(i8 %v, i8 %f, i16 3, i16 4) #1
  %o11 = zext i8 %r11 to i32
  %base = mul i32 %t, 13
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
  store i32 %o7, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x8
  store i32 %o8, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x9
  store i32 %o9, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %x10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x10
  store i32 %o10, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %x11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x11
  store i32 %o11, ptr addrspace(1) %p11, align 4
  %i12 = add i32 %base, 12
  %x12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x12
  store i32 %width, ptr addrspace(1) %p12, align 4
  ret void
}

define void @quad_shuffles_half(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %qlane = and i32 %lane, 3
  %rev = sub i32 3, %qlane
  %rev16 = trunc i32 %rev to i16
  %next = add i32 %qlane, 1
  %next3 = and i32 %next, 3
  %next16 = trunc i32 %next3 to i16
  %m = mul i32 %t, 3
  %vi = add i32 %m, 100
  %fi = add i32 %m, 1000
  %v = uitofp i32 %vi to half
  %f = uitofp i32 %fi to half
  %r0 = call half @air.quad_shuffle.f16(half %v, i16 %rev16) #1
  %o0 = fptoui half %r0 to i32
  %r1 = call half @air.quad_shuffle.f16(half %v, i16 %next16) #1
  %o1 = fptoui half %r1 to i32
  %r2 = call half @air.quad_shuffle_xor.f16(half %v, i16 1) #1
  %o2 = fptoui half %r2 to i32
  %r3 = call half @air.quad_shuffle_xor.f16(half %v, i16 2) #1
  %o3 = fptoui half %r3 to i32
  %r4 = call half @air.quad_shuffle_down.f16(half %v, i16 1) #1
  %o4 = fptoui half %r4 to i32
  %r5 = call half @air.quad_shuffle_down.f16(half %v, i16 2) #1
  %o5 = fptoui half %r5 to i32
  %r6 = call half @air.quad_shuffle_up.f16(half %v, i16 1) #1
  %o6 = fptoui half %r6 to i32
  %r7 = call half @air.quad_shuffle_up.f16(half %v, i16 3) #1
  %o7 = fptoui half %r7 to i32
  %r8 = call half @air.quad_shuffle_and_fill_down.f16(half %v, half %f, i16 1, i16 2) #1
  %o8 = fptoui half %r8 to i32
  %r9 = call half @air.quad_shuffle_and_fill_down.f16(half %v, half %f, i16 2, i16 4) #1
  %o9 = fptoui half %r9 to i32
  %r10 = call half @air.quad_shuffle_and_fill_up.f16(half %v, half %f, i16 1, i16 2) #1
  %o10 = fptoui half %r10 to i32
  %r11 = call half @air.quad_shuffle_and_fill_up.f16(half %v, half %f, i16 3, i16 4) #1
  %o11 = fptoui half %r11 to i32
  %base = mul i32 %t, 13
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
  store i32 %o7, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x8
  store i32 %o8, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x9
  store i32 %o9, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %x10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x10
  store i32 %o10, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %x11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x11
  store i32 %o11, ptr addrspace(1) %p11, align 4
  %i12 = add i32 %base, 12
  %x12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x12
  store i32 %width, ptr addrspace(1) %p12, align 4
  ret void
}

define void @quad_ballot(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %m3 = urem i32 %lane, 3
  %pred = icmp eq i32 %m3, 0
  %b0 = call i16 @air.quad_ballot(i1 %pred) #1
  %o0 = zext i16 %b0 to i32
  %b1 = call i16 @air.quad_ballot(i1 true) #1
  %o1 = zext i16 %b1 to i32
  %odd = and i32 %lane, 1
  %even = icmp eq i32 %odd, 0
  br i1 %even, label %then, label %join

then:
  %b2 = call i16 @air.quad_ballot(i1 true) #1
  %c2 = zext i16 %b2 to i32
  br label %join

join:
  %o2 = phi i32 [ %c2, %then ], [ 77777, %entry ]
  %base = mul i32 %t, 4
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
  store i32 %width, ptr addrspace(1) %p3, align 4
  ret void
}

define void @votes(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %qlane = and i32 %lane, 3
  %s0 = call i64 @air.simd_ballot.i64(i1 true) #1
  %ne1 = icmp ne i32 %lane, 1
  %s1 = call i64 @air.simd_ballot.i64(i1 %ne1) #1
  %s4 = xor i64 %s0, -1
  %q0 = call i16 @air.quad_ballot(i1 true) #1
  %ne2 = icmp ne i32 %qlane, 2
  %q1 = call i16 @air.quad_ballot(i1 %ne2) #1
  %sa0 = call i1 @air.simd_vote_all.i64(i64 %s0) #1
  %sao0 = zext i1 %sa0 to i32
  %sn0 = call i1 @air.simd_vote_any.i64(i64 %s0) #1
  %sno0 = zext i1 %sn0 to i32
  %sa1 = call i1 @air.simd_vote_all.i64(i64 %s1) #1
  %sao1 = zext i1 %sa1 to i32
  %sn1 = call i1 @air.simd_vote_any.i64(i64 %s1) #1
  %sno1 = zext i1 %sn1 to i32
  %sa2 = call i1 @air.simd_vote_all.i64(i64 0) #1
  %sao2 = zext i1 %sa2 to i32
  %sn2 = call i1 @air.simd_vote_any.i64(i64 0) #1
  %sno2 = zext i1 %sn2 to i32
  %sa3 = call i1 @air.simd_vote_all.i64(i64 -1) #1
  %sao3 = zext i1 %sa3 to i32
  %sn3 = call i1 @air.simd_vote_any.i64(i64 -1) #1
  %sno3 = zext i1 %sn3 to i32
  %sa4 = call i1 @air.simd_vote_all.i64(i64 %s4) #1
  %sao4 = zext i1 %sa4 to i32
  %sn4 = call i1 @air.simd_vote_any.i64(i64 %s4) #1
  %sno4 = zext i1 %sn4 to i32
  %qa0 = call i1 @air.quad_vote_all(i16 %q0) #1
  %qao0 = zext i1 %qa0 to i32
  %qn0 = call i1 @air.quad_vote_any(i16 %q0) #1
  %qno0 = zext i1 %qn0 to i32
  %qa1 = call i1 @air.quad_vote_all(i16 %q1) #1
  %qao1 = zext i1 %qa1 to i32
  %qn1 = call i1 @air.quad_vote_any(i16 %q1) #1
  %qno1 = zext i1 %qn1 to i32
  %qa2 = call i1 @air.quad_vote_all(i16 -16) #1
  %qao2 = zext i1 %qa2 to i32
  %qn2 = call i1 @air.quad_vote_any(i16 -16) #1
  %qno2 = zext i1 %qn2 to i32
  %qa3 = call i1 @air.quad_vote_all(i16 15) #1
  %qao3 = zext i1 %qa3 to i32
  %qn3 = call i1 @air.quad_vote_any(i16 15) #1
  %qno3 = zext i1 %qn3 to i32
  %qa4 = call i1 @air.quad_vote_all(i16 1) #1
  %qao4 = zext i1 %qa4 to i32
  %qn4 = call i1 @air.quad_vote_any(i16 1) #1
  %qno4 = zext i1 %qn4 to i32
  %base = mul i32 %t, 21
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %sao0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %sno0, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %sao1, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %sno1, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %sao2, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %sno2, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %sao3, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %sno3, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x8
  store i32 %sao4, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x9
  store i32 %sno4, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %x10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x10
  store i32 %qao0, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %x11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x11
  store i32 %qno0, ptr addrspace(1) %p11, align 4
  %i12 = add i32 %base, 12
  %x12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x12
  store i32 %qao1, ptr addrspace(1) %p12, align 4
  %i13 = add i32 %base, 13
  %x13 = zext i32 %i13 to i64
  %p13 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x13
  store i32 %qno1, ptr addrspace(1) %p13, align 4
  %i14 = add i32 %base, 14
  %x14 = zext i32 %i14 to i64
  %p14 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x14
  store i32 %qao2, ptr addrspace(1) %p14, align 4
  %i15 = add i32 %base, 15
  %x15 = zext i32 %i15 to i64
  %p15 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x15
  store i32 %qno2, ptr addrspace(1) %p15, align 4
  %i16 = add i32 %base, 16
  %x16 = zext i32 %i16 to i64
  %p16 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x16
  store i32 %qao3, ptr addrspace(1) %p16, align 4
  %i17 = add i32 %base, 17
  %x17 = zext i32 %i17 to i64
  %p17 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x17
  store i32 %qno3, ptr addrspace(1) %p17, align 4
  %i18 = add i32 %base, 18
  %x18 = zext i32 %i18 to i64
  %p18 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x18
  store i32 %qao4, ptr addrspace(1) %p18, align 4
  %i19 = add i32 %base, 19
  %x19 = zext i32 %i19 to i64
  %p19 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x19
  store i32 %qno4, ptr addrspace(1) %p19, align 4
  %i20 = add i32 %base, 20
  %x20 = zext i32 %i20 to i64
  %p20 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x20
  store i32 %width, ptr addrspace(1) %p20, align 4
  ret void
}

declare i32 @air.quad_shuffle.s.i32(i32, i16) local_unnamed_addr #1
declare i32 @air.quad_shuffle_xor.s.i32(i32, i16) local_unnamed_addr #1
declare i32 @air.quad_shuffle_down.s.i32(i32, i16) local_unnamed_addr #1
declare i32 @air.quad_shuffle_up.s.i32(i32, i16) local_unnamed_addr #1
declare i32 @air.quad_shuffle_and_fill_down.s.i32(i32, i32, i16, i16) local_unnamed_addr #1
declare i32 @air.quad_shuffle_and_fill_up.s.i32(i32, i32, i16, i16) local_unnamed_addr #1
declare float @air.quad_shuffle.f32(float, i16) local_unnamed_addr #1
declare float @air.quad_shuffle_xor.f32(float, i16) local_unnamed_addr #1
declare float @air.quad_shuffle_down.f32(float, i16) local_unnamed_addr #1
declare float @air.quad_shuffle_up.f32(float, i16) local_unnamed_addr #1
declare float @air.quad_shuffle_and_fill_down.f32(float, float, i16, i16) local_unnamed_addr #1
declare float @air.quad_shuffle_and_fill_up.f32(float, float, i16, i16) local_unnamed_addr #1
declare i8 @air.quad_shuffle.u.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.quad_shuffle_xor.u.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.quad_shuffle_down.u.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.quad_shuffle_up.u.i8(i8, i16) local_unnamed_addr #1
declare i8 @air.quad_shuffle_and_fill_down.u.i8(i8, i8, i16, i16) local_unnamed_addr #1
declare i8 @air.quad_shuffle_and_fill_up.u.i8(i8, i8, i16, i16) local_unnamed_addr #1
declare half @air.quad_shuffle.f16(half, i16) local_unnamed_addr #1
declare half @air.quad_shuffle_xor.f16(half, i16) local_unnamed_addr #1
declare half @air.quad_shuffle_down.f16(half, i16) local_unnamed_addr #1
declare half @air.quad_shuffle_up.f16(half, i16) local_unnamed_addr #1
declare half @air.quad_shuffle_and_fill_down.f16(half, half, i16, i16) local_unnamed_addr #1
declare half @air.quad_shuffle_and_fill_up.f16(half, half, i16, i16) local_unnamed_addr #1
declare i16 @air.quad_ballot(i1) local_unnamed_addr #1
declare i64 @air.simd_ballot.i64(i1) local_unnamed_addr #1
declare i1 @air.simd_vote_all.i64(i64) local_unnamed_addr #1
declare i1 @air.simd_vote_any.i64(i64) local_unnamed_addr #1
declare i1 @air.quad_vote_all(i16) local_unnamed_addr #1
declare i1 @air.quad_vote_any(i16) local_unnamed_addr #1

attributes #0 = { nounwind }
attributes #1 = { convergent nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!10, !11, !12, !13, !14, !15}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 4, i32 0}
!2 = !{!"Metal", i32 2, i32 4, i32 0}
!3 = !{}
!4 = !{!5, !6, !7, !8}
!5 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!6 = !{i32 1, !"air.thread_position_in_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"t"}
!7 = !{i32 2, !"air.thread_index_in_simdgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"lane"}
!8 = !{i32 3, !"air.threads_per_simdgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"width"}
!10 = !{ptr @quad_shuffles, !3, !4}
!11 = !{ptr @quad_shuffles_float, !3, !4}
!12 = !{ptr @quad_shuffles_char, !3, !4}
!13 = !{ptr @quad_shuffles_half, !3, !4}
!14 = !{ptr @quad_ballot, !3, !4}
!15 = !{ptr @votes, !3, !4}


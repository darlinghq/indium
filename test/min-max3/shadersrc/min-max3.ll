; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the min-max3 test: air.min3, air.max3 and air.median3, signed and unsigned, on 8, 16, 32
; and 64-bit integers and a vector. Thread id reads in[id], in[(id + 8) % 16] and in[(id + 4) % 16] and writes
; its results to out[id * n + k]; see min-max3.cpp for what each slot holds. See README.md for how this becomes
; min-max3.metallib.

source_filename = "min-max3"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @minmax3_i8(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
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
  %min3s = call i8 @air.min3.s.i8(i8 %a, i8 %b, i8 %c)
  %min3sx = sext i8 %min3s to i32
  %min3u = call i8 @air.min3.u.i8(i8 %a, i8 %b, i8 %c)
  %min3ux = zext i8 %min3u to i32
  %max3s = call i8 @air.max3.s.i8(i8 %a, i8 %b, i8 %c)
  %max3sx = sext i8 %max3s to i32
  %max3u = call i8 @air.max3.u.i8(i8 %a, i8 %b, i8 %c)
  %max3ux = zext i8 %max3u to i32
  %median3s = call i8 @air.median3.s.i8(i8 %a, i8 %b, i8 %c)
  %median3sx = sext i8 %median3s to i32
  %median3u = call i8 @air.median3.u.i8(i8 %a, i8 %b, i8 %c)
  %median3ux = zext i8 %median3u to i32
  %base = mul i32 %id, 6
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %min3sx, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %min3ux, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %max3sx, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %max3ux, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %o4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o4
  store i32 %median3sx, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %o5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o5
  store i32 %median3ux, ptr addrspace(1) %p5, align 4
  ret void
}

define void @minmax3_i16(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
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
  %min3s = call i16 @air.min3.s.i16(i16 %a, i16 %b, i16 %c)
  %min3sx = sext i16 %min3s to i32
  %min3u = call i16 @air.min3.u.i16(i16 %a, i16 %b, i16 %c)
  %min3ux = zext i16 %min3u to i32
  %max3s = call i16 @air.max3.s.i16(i16 %a, i16 %b, i16 %c)
  %max3sx = sext i16 %max3s to i32
  %max3u = call i16 @air.max3.u.i16(i16 %a, i16 %b, i16 %c)
  %max3ux = zext i16 %max3u to i32
  %median3s = call i16 @air.median3.s.i16(i16 %a, i16 %b, i16 %c)
  %median3sx = sext i16 %median3s to i32
  %median3u = call i16 @air.median3.u.i16(i16 %a, i16 %b, i16 %c)
  %median3ux = zext i16 %median3u to i32
  %base = mul i32 %id, 6
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %min3sx, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %min3ux, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %max3sx, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %max3ux, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %o4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o4
  store i32 %median3sx, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %o5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o5
  store i32 %median3ux, ptr addrspace(1) %p5, align 4
  ret void
}

define void @minmax3_i32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
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
  %min3s = call i32 @air.min3.s.i32(i32 %a, i32 %b, i32 %c)
  %min3u = call i32 @air.min3.u.i32(i32 %a, i32 %b, i32 %c)
  %max3s = call i32 @air.max3.s.i32(i32 %a, i32 %b, i32 %c)
  %max3u = call i32 @air.max3.u.i32(i32 %a, i32 %b, i32 %c)
  %median3s = call i32 @air.median3.s.i32(i32 %a, i32 %b, i32 %c)
  %median3u = call i32 @air.median3.u.i32(i32 %a, i32 %b, i32 %c)
  %base = mul i32 %id, 6
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %min3s, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %min3u, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %max3s, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %max3u, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %o4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o4
  store i32 %median3s, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %o5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o5
  store i32 %median3u, ptr addrspace(1) %p5, align 4
  ret void
}

define void @minmax3_i64(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
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
  %min3s = call i64 @air.min3.s.i64(i64 %a, i64 %b, i64 %c)
  %min3slo = trunc i64 %min3s to i32
  %min3ss = lshr i64 %min3s, 32
  %min3shi = trunc i64 %min3ss to i32
  %min3u = call i64 @air.min3.u.i64(i64 %a, i64 %b, i64 %c)
  %min3ulo = trunc i64 %min3u to i32
  %min3us = lshr i64 %min3u, 32
  %min3uhi = trunc i64 %min3us to i32
  %max3s = call i64 @air.max3.s.i64(i64 %a, i64 %b, i64 %c)
  %max3slo = trunc i64 %max3s to i32
  %max3ss = lshr i64 %max3s, 32
  %max3shi = trunc i64 %max3ss to i32
  %max3u = call i64 @air.max3.u.i64(i64 %a, i64 %b, i64 %c)
  %max3ulo = trunc i64 %max3u to i32
  %max3us = lshr i64 %max3u, 32
  %max3uhi = trunc i64 %max3us to i32
  %median3s = call i64 @air.median3.s.i64(i64 %a, i64 %b, i64 %c)
  %median3slo = trunc i64 %median3s to i32
  %median3ss = lshr i64 %median3s, 32
  %median3shi = trunc i64 %median3ss to i32
  %median3u = call i64 @air.median3.u.i64(i64 %a, i64 %b, i64 %c)
  %median3ulo = trunc i64 %median3u to i32
  %median3us = lshr i64 %median3u, 32
  %median3uhi = trunc i64 %median3us to i32
  %base = mul i32 %id, 12
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %min3slo, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %min3shi, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %min3ulo, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %min3uhi, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %o4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o4
  store i32 %max3slo, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %o5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o5
  store i32 %max3shi, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %o6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o6
  store i32 %max3ulo, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %o7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o7
  store i32 %max3uhi, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %o8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o8
  store i32 %median3slo, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %o9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o9
  store i32 %median3shi, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %o10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o10
  store i32 %median3ulo, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %o11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o11
  store i32 %median3uhi, ptr addrspace(1) %p11, align 4
  ret void
}

define void @minmax3_v2i32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
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
  %min3s = call <2 x i32> @air.min3.s.v2i32(<2 x i32> %a, <2 x i32> %b, <2 x i32> %c)
  %min3s0 = extractelement <2 x i32> %min3s, i32 0
  %min3s1 = extractelement <2 x i32> %min3s, i32 1
  %min3u = call <2 x i32> @air.min3.u.v2i32(<2 x i32> %a, <2 x i32> %b, <2 x i32> %c)
  %min3u0 = extractelement <2 x i32> %min3u, i32 0
  %min3u1 = extractelement <2 x i32> %min3u, i32 1
  %max3s = call <2 x i32> @air.max3.s.v2i32(<2 x i32> %a, <2 x i32> %b, <2 x i32> %c)
  %max3s0 = extractelement <2 x i32> %max3s, i32 0
  %max3s1 = extractelement <2 x i32> %max3s, i32 1
  %max3u = call <2 x i32> @air.max3.u.v2i32(<2 x i32> %a, <2 x i32> %b, <2 x i32> %c)
  %max3u0 = extractelement <2 x i32> %max3u, i32 0
  %max3u1 = extractelement <2 x i32> %max3u, i32 1
  %median3s = call <2 x i32> @air.median3.s.v2i32(<2 x i32> %a, <2 x i32> %b, <2 x i32> %c)
  %median3s0 = extractelement <2 x i32> %median3s, i32 0
  %median3s1 = extractelement <2 x i32> %median3s, i32 1
  %median3u = call <2 x i32> @air.median3.u.v2i32(<2 x i32> %a, <2 x i32> %b, <2 x i32> %c)
  %median3u0 = extractelement <2 x i32> %median3u, i32 0
  %median3u1 = extractelement <2 x i32> %median3u, i32 1
  %base = mul i32 %id, 12
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %min3s0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %min3s1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %min3u0, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %min3u1, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %o4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o4
  store i32 %max3s0, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %o5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o5
  store i32 %max3s1, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %o6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o6
  store i32 %max3u0, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %o7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o7
  store i32 %max3u1, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %o8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o8
  store i32 %median3s0, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %o9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o9
  store i32 %median3s1, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %o10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o10
  store i32 %median3u0, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %o11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o11
  store i32 %median3u1, ptr addrspace(1) %p11, align 4
  ret void
}

declare i8 @air.min3.s.i8(i8, i8, i8)
declare i8 @air.min3.u.i8(i8, i8, i8)
declare i8 @air.max3.s.i8(i8, i8, i8)
declare i8 @air.max3.u.i8(i8, i8, i8)
declare i8 @air.median3.s.i8(i8, i8, i8)
declare i8 @air.median3.u.i8(i8, i8, i8)
declare i16 @air.min3.s.i16(i16, i16, i16)
declare i16 @air.min3.u.i16(i16, i16, i16)
declare i16 @air.max3.s.i16(i16, i16, i16)
declare i16 @air.max3.u.i16(i16, i16, i16)
declare i16 @air.median3.s.i16(i16, i16, i16)
declare i16 @air.median3.u.i16(i16, i16, i16)
declare i32 @air.min3.s.i32(i32, i32, i32)
declare i32 @air.min3.u.i32(i32, i32, i32)
declare i32 @air.max3.s.i32(i32, i32, i32)
declare i32 @air.max3.u.i32(i32, i32, i32)
declare i32 @air.median3.s.i32(i32, i32, i32)
declare i32 @air.median3.u.i32(i32, i32, i32)
declare i64 @air.min3.s.i64(i64, i64, i64)
declare i64 @air.min3.u.i64(i64, i64, i64)
declare i64 @air.max3.s.i64(i64, i64, i64)
declare i64 @air.max3.u.i64(i64, i64, i64)
declare i64 @air.median3.s.i64(i64, i64, i64)
declare i64 @air.median3.u.i64(i64, i64, i64)
declare <2 x i32> @air.min3.s.v2i32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @air.min3.u.v2i32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @air.max3.s.v2i32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @air.max3.u.v2i32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @air.median3.s.v2i32(<2 x i32>, <2 x i32>, <2 x i32>)
declare <2 x i32> @air.median3.u.v2i32(<2 x i32>, <2 x i32>, <2 x i32>)

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
!10 = !{ptr @minmax3_i8, !3, !4}
!11 = !{ptr @minmax3_i16, !3, !4}
!12 = !{ptr @minmax3_i32, !3, !4}
!13 = !{ptr @minmax3_i64, !3, !4}
!14 = !{ptr @minmax3_v2i32, !3, !4}

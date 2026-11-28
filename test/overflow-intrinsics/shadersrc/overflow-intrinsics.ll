; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the overflow-intrinsics test: the LLVM arithmetic intrinsics with an overflow check,
; llvm.<s|u><add|sub|mul>.with.overflow, on 16, 32 and 64-bit integers and a vector. Thread id combines
; in[id] with in[(id + 8) % 16] and writes its results to out[id * n + k]; see overflow-intrinsics.cpp for what each
; slot holds. See README.md for how this becomes overflow-intrinsics.metallib.

source_filename = "overflow-intrinsics"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @overflow_i16(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %a = trunc i32 %va to i16
  %b = trunc i32 %vb to i16
  %sadd = call { i16, i1 } @llvm.sadd.with.overflow.i16(i16 %a, i16 %b)
  %saddv = extractvalue { i16, i1 } %sadd, 0
  %saddo = extractvalue { i16, i1 } %sadd, 1
  %saddr1 = zext i1 %saddo to i32
  %saddr0 = zext i16 %saddv to i32
  %uadd = call { i16, i1 } @llvm.uadd.with.overflow.i16(i16 %a, i16 %b)
  %uaddv = extractvalue { i16, i1 } %uadd, 0
  %uaddo = extractvalue { i16, i1 } %uadd, 1
  %uaddr1 = zext i1 %uaddo to i32
  %uaddr0 = zext i16 %uaddv to i32
  %ssub = call { i16, i1 } @llvm.ssub.with.overflow.i16(i16 %a, i16 %b)
  %ssubv = extractvalue { i16, i1 } %ssub, 0
  %ssubo = extractvalue { i16, i1 } %ssub, 1
  %ssubr1 = zext i1 %ssubo to i32
  %ssubr0 = zext i16 %ssubv to i32
  %usub = call { i16, i1 } @llvm.usub.with.overflow.i16(i16 %a, i16 %b)
  %usubv = extractvalue { i16, i1 } %usub, 0
  %usubo = extractvalue { i16, i1 } %usub, 1
  %usubr1 = zext i1 %usubo to i32
  %usubr0 = zext i16 %usubv to i32
  %smul = call { i16, i1 } @llvm.smul.with.overflow.i16(i16 %a, i16 %b)
  %smulv = extractvalue { i16, i1 } %smul, 0
  %smulo = extractvalue { i16, i1 } %smul, 1
  %smulr1 = zext i1 %smulo to i32
  %smulr0 = zext i16 %smulv to i32
  %umul = call { i16, i1 } @llvm.umul.with.overflow.i16(i16 %a, i16 %b)
  %umulv = extractvalue { i16, i1 } %umul, 0
  %umulo = extractvalue { i16, i1 } %umul, 1
  %umulr1 = zext i1 %umulo to i32
  %umulr0 = zext i16 %umulv to i32
  %base = mul i32 %id, 12
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %saddr0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %saddr1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %uaddr0, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %uaddr1, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %ssubr0, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %ssubr1, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %usubr0, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %usubr1, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x8
  store i32 %smulr0, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x9
  store i32 %smulr1, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %x10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x10
  store i32 %umulr0, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %x11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x11
  store i32 %umulr1, ptr addrspace(1) %p11, align 4
  ret void
}

define void @overflow_i32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %a = add i32 %va, 0
  %b = add i32 %vb, 0
  %sadd = call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %a, i32 %b)
  %saddv = extractvalue { i32, i1 } %sadd, 0
  %saddo = extractvalue { i32, i1 } %sadd, 1
  %saddr1 = zext i1 %saddo to i32
  %saddr0 = add i32 %saddv, 0
  %uadd = call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %a, i32 %b)
  %uaddv = extractvalue { i32, i1 } %uadd, 0
  %uaddo = extractvalue { i32, i1 } %uadd, 1
  %uaddr1 = zext i1 %uaddo to i32
  %uaddr0 = add i32 %uaddv, 0
  %ssub = call { i32, i1 } @llvm.ssub.with.overflow.i32(i32 %a, i32 %b)
  %ssubv = extractvalue { i32, i1 } %ssub, 0
  %ssubo = extractvalue { i32, i1 } %ssub, 1
  %ssubr1 = zext i1 %ssubo to i32
  %ssubr0 = add i32 %ssubv, 0
  %usub = call { i32, i1 } @llvm.usub.with.overflow.i32(i32 %a, i32 %b)
  %usubv = extractvalue { i32, i1 } %usub, 0
  %usubo = extractvalue { i32, i1 } %usub, 1
  %usubr1 = zext i1 %usubo to i32
  %usubr0 = add i32 %usubv, 0
  %smul = call { i32, i1 } @llvm.smul.with.overflow.i32(i32 %a, i32 %b)
  %smulv = extractvalue { i32, i1 } %smul, 0
  %smulo = extractvalue { i32, i1 } %smul, 1
  %smulr1 = zext i1 %smulo to i32
  %smulr0 = add i32 %smulv, 0
  %umul = call { i32, i1 } @llvm.umul.with.overflow.i32(i32 %a, i32 %b)
  %umulv = extractvalue { i32, i1 } %umul, 0
  %umulo = extractvalue { i32, i1 } %umul, 1
  %umulr1 = zext i1 %umulo to i32
  %umulr0 = add i32 %umulv, 0
  %pa0 = insertelement <2 x i32> undef, i32 %a, i64 0
  %pa1 = insertelement <2 x i32> %pa0, i32 %a, i64 1
  %pb0 = insertelement <2 x i32> undef, i32 %b, i64 0
  %pb1 = insertelement <2 x i32> %pb0, i32 2, i64 1
  %vmul = call { <2 x i32>, <2 x i1> } @llvm.smul.with.overflow.v2i32(<2 x i32> %pa1, <2 x i32> %pb1)
  %vmulv = extractvalue { <2 x i32>, <2 x i1> } %vmul, 0
  %vmulo = extractvalue { <2 x i32>, <2 x i1> } %vmul, 1
  %vmulo32 = zext <2 x i1> %vmulo to <2 x i32>
  %vmulr0 = extractelement <2 x i32> %vmulv, i64 1
  %vmulr1 = extractelement <2 x i32> %vmulo32, i64 0
  %vmulr2 = extractelement <2 x i32> %vmulo32, i64 1
  %base = mul i32 %id, 15
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %saddr0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %saddr1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %uaddr0, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %uaddr1, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %ssubr0, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %ssubr1, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %usubr0, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %usubr1, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x8
  store i32 %smulr0, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x9
  store i32 %smulr1, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %x10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x10
  store i32 %umulr0, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %x11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x11
  store i32 %umulr1, ptr addrspace(1) %p11, align 4
  %i12 = add i32 %base, 12
  %x12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x12
  store i32 %vmulr0, ptr addrspace(1) %p12, align 4
  %i13 = add i32 %base, 13
  %x13 = zext i32 %i13 to i64
  %p13 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x13
  store i32 %vmulr1, ptr addrspace(1) %p13, align 4
  %i14 = add i32 %base, 14
  %x14 = zext i32 %i14 to i64
  %p14 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x14
  store i32 %vmulr2, ptr addrspace(1) %p14, align 4
  ret void
}

define void @overflow_i64(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  ; a = va * 2^32 + vb, b = vb * 2^32 + va
  %sa = sext i32 %va to i64
  %sb = sext i32 %vb to i64
  %za = zext i32 %va to i64
  %zb = zext i32 %vb to i64
  %ha = shl i64 %sa, 32
  %hb = shl i64 %sb, 32
  %a = or i64 %ha, %zb
  %b = or i64 %hb, %za
  %sadd = call { i64, i1 } @llvm.sadd.with.overflow.i64(i64 %a, i64 %b)
  %saddv = extractvalue { i64, i1 } %sadd, 0
  %saddo = extractvalue { i64, i1 } %sadd, 1
  %saddr1 = zext i1 %saddo to i32
  %saddr0 = trunc i64 %saddv to i32
  %saddh = lshr i64 %saddv, 32
  %saddr2 = trunc i64 %saddh to i32
  %uadd = call { i64, i1 } @llvm.uadd.with.overflow.i64(i64 %a, i64 %b)
  %uaddv = extractvalue { i64, i1 } %uadd, 0
  %uaddo = extractvalue { i64, i1 } %uadd, 1
  %uaddr1 = zext i1 %uaddo to i32
  %uaddr0 = trunc i64 %uaddv to i32
  %uaddh = lshr i64 %uaddv, 32
  %uaddr2 = trunc i64 %uaddh to i32
  %ssub = call { i64, i1 } @llvm.ssub.with.overflow.i64(i64 %a, i64 %b)
  %ssubv = extractvalue { i64, i1 } %ssub, 0
  %ssubo = extractvalue { i64, i1 } %ssub, 1
  %ssubr1 = zext i1 %ssubo to i32
  %ssubr0 = trunc i64 %ssubv to i32
  %ssubh = lshr i64 %ssubv, 32
  %ssubr2 = trunc i64 %ssubh to i32
  %usub = call { i64, i1 } @llvm.usub.with.overflow.i64(i64 %a, i64 %b)
  %usubv = extractvalue { i64, i1 } %usub, 0
  %usubo = extractvalue { i64, i1 } %usub, 1
  %usubr1 = zext i1 %usubo to i32
  %usubr0 = trunc i64 %usubv to i32
  %usubh = lshr i64 %usubv, 32
  %usubr2 = trunc i64 %usubh to i32
  %smul = call { i64, i1 } @llvm.smul.with.overflow.i64(i64 %a, i64 %b)
  %smulv = extractvalue { i64, i1 } %smul, 0
  %smulo = extractvalue { i64, i1 } %smul, 1
  %smulr1 = zext i1 %smulo to i32
  %smulr0 = trunc i64 %smulv to i32
  %smulh = lshr i64 %smulv, 32
  %smulr2 = trunc i64 %smulh to i32
  %umul = call { i64, i1 } @llvm.umul.with.overflow.i64(i64 %a, i64 %b)
  %umulv = extractvalue { i64, i1 } %umul, 0
  %umulo = extractvalue { i64, i1 } %umul, 1
  %umulr1 = zext i1 %umulo to i32
  %umulr0 = trunc i64 %umulv to i32
  %umulh = lshr i64 %umulv, 32
  %umulr2 = trunc i64 %umulh to i32
  %base = mul i32 %id, 18
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %saddr0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %saddr2, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %saddr1, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %uaddr0, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %uaddr2, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %uaddr1, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %ssubr0, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %ssubr2, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x8
  store i32 %ssubr1, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x9
  store i32 %usubr0, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %x10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x10
  store i32 %usubr2, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %x11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x11
  store i32 %usubr1, ptr addrspace(1) %p11, align 4
  %i12 = add i32 %base, 12
  %x12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x12
  store i32 %smulr0, ptr addrspace(1) %p12, align 4
  %i13 = add i32 %base, 13
  %x13 = zext i32 %i13 to i64
  %p13 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x13
  store i32 %smulr2, ptr addrspace(1) %p13, align 4
  %i14 = add i32 %base, 14
  %x14 = zext i32 %i14 to i64
  %p14 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x14
  store i32 %smulr1, ptr addrspace(1) %p14, align 4
  %i15 = add i32 %base, 15
  %x15 = zext i32 %i15 to i64
  %p15 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x15
  store i32 %umulr0, ptr addrspace(1) %p15, align 4
  %i16 = add i32 %base, 16
  %x16 = zext i32 %i16 to i64
  %p16 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x16
  store i32 %umulr2, ptr addrspace(1) %p16, align 4
  %i17 = add i32 %base, 17
  %x17 = zext i32 %i17 to i64
  %p17 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x17
  store i32 %umulr1, ptr addrspace(1) %p17, align 4
  ret void
}

declare { i16, i1 } @llvm.sadd.with.overflow.i16(i16, i16)
declare { i16, i1 } @llvm.uadd.with.overflow.i16(i16, i16)
declare { i16, i1 } @llvm.ssub.with.overflow.i16(i16, i16)
declare { i16, i1 } @llvm.usub.with.overflow.i16(i16, i16)
declare { i16, i1 } @llvm.smul.with.overflow.i16(i16, i16)
declare { i16, i1 } @llvm.umul.with.overflow.i16(i16, i16)
declare { i32, i1 } @llvm.sadd.with.overflow.i32(i32, i32)
declare { i32, i1 } @llvm.uadd.with.overflow.i32(i32, i32)
declare { i32, i1 } @llvm.ssub.with.overflow.i32(i32, i32)
declare { i32, i1 } @llvm.usub.with.overflow.i32(i32, i32)
declare { i32, i1 } @llvm.smul.with.overflow.i32(i32, i32)
declare { i32, i1 } @llvm.umul.with.overflow.i32(i32, i32)
declare { i64, i1 } @llvm.sadd.with.overflow.i64(i64, i64)
declare { i64, i1 } @llvm.uadd.with.overflow.i64(i64, i64)
declare { i64, i1 } @llvm.ssub.with.overflow.i64(i64, i64)
declare { i64, i1 } @llvm.usub.with.overflow.i64(i64, i64)
declare { i64, i1 } @llvm.smul.with.overflow.i64(i64, i64)
declare { i64, i1 } @llvm.umul.with.overflow.i64(i64, i64)
declare { <2 x i32>, <2 x i1> } @llvm.smul.with.overflow.v2i32(<2 x i32>, <2 x i32>)

attributes #0 = { nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!3, !8, !9}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 2, i32 0}
!2 = !{!"Metal", i32 2, i32 2, i32 0}

!3 = !{ptr @overflow_i16, !4, !5}
!4 = !{}
!5 = !{!6, !7, !10}
!6 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}
!7 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"in"}
!10 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}
!8 = !{ptr @overflow_i32, !4, !5}
!9 = !{ptr @overflow_i64, !4, !5}

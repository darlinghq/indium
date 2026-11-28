; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the math-functions test: the inverse hyperbolic functions, exp10, log10, ldexp, nextafter,
; sincos, sinpi, cospi and tanpi on float and half, and llvm.copysign. Thread id reads in[id] (and, for the
; functions of two arguments, in[(id + 8) % 16]) and writes its results to out[id * n + k]; see
; math-functions.cpp for what each slot holds. See README.md for how this becomes math-functions.metallib.

source_filename = "math-functions"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @functions_f32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %cosval = alloca float, align 4
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %x = bitcast i32 %va to float
  %y = bitcast i32 %vb to float
  %asinh = call float @air.asinh.f32(float %x)
  %ax = call float @air.fabs.f32(float %x)
  %ax1 = fadd float %ax, 1.0
  %acosh = call float @air.acosh.f32(float %ax1)
  %x16 = fmul float %x, 0.0625
  %atanh = call float @air.atanh.f32(float %x16)
  %x4 = fmul float %x, 0.25
  %exp10 = call float @air.exp10.f32(float %x4)
  %log10 = call float @air.log10.f32(float %ax)
  %copysign = call float @llvm.copysign.f32(float %x, float %y)
  %sin = call float @air.sincos.f32(float %x, ptr %cosval)
  %cos = load float, ptr %cosval, align 4
  %next = call float @air.nextafter.f32(float %x, float %y)
  %nextzero = call float @air.nextafter.f32(float %x, float 0.0)
  %nextfromzero = call float @air.nextafter.f32(float -0.0, float %x)
  %nextsame = call float @air.nextafter.f32(float %x, float %x)
  %unit = call float @llvm.copysign.f32(float 1.0, float %x)
  %pole = call float @air.atanh.f32(float %unit)
  %rasinh = bitcast float %asinh to i32
  %racosh = bitcast float %acosh to i32
  %ratanh = bitcast float %atanh to i32
  %rexp10 = bitcast float %exp10 to i32
  %rlog10 = bitcast float %log10 to i32
  %rcopysign = bitcast float %copysign to i32
  %rsin = bitcast float %sin to i32
  %rcos = bitcast float %cos to i32
  %rnext = bitcast float %next to i32
  %rnextzero = bitcast float %nextzero to i32
  %rnextfromzero = bitcast float %nextfromzero to i32
  %rnextsame = bitcast float %nextsame to i32
  %rpole = bitcast float %pole to i32
  %base = mul i32 %id, 13
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %rasinh, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %racosh, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %ratanh, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %rexp10, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %o4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o4
  store i32 %rlog10, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %o5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o5
  store i32 %rcopysign, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %o6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o6
  store i32 %rsin, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %o7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o7
  store i32 %rcos, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %o8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o8
  store i32 %rnext, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %o9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o9
  store i32 %rnextzero, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %o10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o10
  store i32 %rnextfromzero, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %o11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o11
  store i32 %rnextsame, ptr addrspace(1) %p11, align 4
  %i12 = add i32 %base, 12
  %o12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o12
  store i32 %rpole, ptr addrspace(1) %p12, align 4
  ret void
}

define void @functions_f16(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %cosval = alloca half, align 2
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %ta = trunc i32 %va to i16
  %x = bitcast i16 %ta to half
  %tb = trunc i32 %vb to i16
  %y = bitcast i16 %tb to half
  %asinh = call half @air.asinh.f16(half %x)
  %ax = call half @air.fabs.f16(half %x)
  %ax1 = fadd half %ax, 1.0
  %acosh = call half @air.acosh.f16(half %ax1)
  %x16 = fmul half %x, 0.0625
  %atanh = call half @air.atanh.f16(half %x16)
  %x4 = fmul half %x, 0.25
  %exp10 = call half @air.exp10.f16(half %x4)
  %log10 = call half @air.log10.f16(half %ax)
  %copysign = call half @llvm.copysign.f16(half %x, half %y)
  %sin = call half @air.sincos.f16(half %x, ptr %cosval)
  %cos = load half, ptr %cosval, align 2
  %next = call half @air.nextafter.f16(half %x, half %y)
  %nextzero = call half @air.nextafter.f16(half %x, half 0.0)
  %nextfromzero = call half @air.nextafter.f16(half -0.0, half %x)
  %nextsame = call half @air.nextafter.f16(half %x, half %x)
  %unit = call half @llvm.copysign.f16(half 1.0, half %x)
  %pole = call half @air.atanh.f16(half %unit)
  %rasinhh = bitcast half %asinh to i16
  %rasinh = zext i16 %rasinhh to i32
  %racoshh = bitcast half %acosh to i16
  %racosh = zext i16 %racoshh to i32
  %ratanhh = bitcast half %atanh to i16
  %ratanh = zext i16 %ratanhh to i32
  %rexp10h = bitcast half %exp10 to i16
  %rexp10 = zext i16 %rexp10h to i32
  %rlog10h = bitcast half %log10 to i16
  %rlog10 = zext i16 %rlog10h to i32
  %rcopysignh = bitcast half %copysign to i16
  %rcopysign = zext i16 %rcopysignh to i32
  %rsinh = bitcast half %sin to i16
  %rsin = zext i16 %rsinh to i32
  %rcosh = bitcast half %cos to i16
  %rcos = zext i16 %rcosh to i32
  %rnexth = bitcast half %next to i16
  %rnext = zext i16 %rnexth to i32
  %rnextzeroh = bitcast half %nextzero to i16
  %rnextzero = zext i16 %rnextzeroh to i32
  %rnextfromzeroh = bitcast half %nextfromzero to i16
  %rnextfromzero = zext i16 %rnextfromzeroh to i32
  %rnextsameh = bitcast half %nextsame to i16
  %rnextsame = zext i16 %rnextsameh to i32
  %rpoleh = bitcast half %pole to i16
  %rpole = zext i16 %rpoleh to i32
  %base = mul i32 %id, 13
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %rasinh, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %racosh, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %ratanh, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %rexp10, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %o4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o4
  store i32 %rlog10, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %o5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o5
  store i32 %rcopysign, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %o6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o6
  store i32 %rsin, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %o7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o7
  store i32 %rcos, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %o8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o8
  store i32 %rnext, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %o9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o9
  store i32 %rnextzero, ptr addrspace(1) %p9, align 4
  %i10 = add i32 %base, 10
  %o10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o10
  store i32 %rnextfromzero, ptr addrspace(1) %p10, align 4
  %i11 = add i32 %base, 11
  %o11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o11
  store i32 %rnextsame, ptr addrspace(1) %p11, align 4
  %i12 = add i32 %base, 12
  %o12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o12
  store i32 %rpole, ptr addrspace(1) %p12, align 4
  ret void
}

define void @ldexp_f32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %x = bitcast i32 %va to float
  %ldexp = call float @air.ldexp.f32(float %x, i32 %vb)
  %v0 = insertelement <2 x float> poison, float %x, i32 0
  %v = insertelement <2 x float> %v0, float %x, i32 1
  %k0 = insertelement <2 x i32> poison, i32 %vb, i32 0
  %kn = sub i32 0, %vb
  %k = insertelement <2 x i32> %k0, i32 %kn, i32 1
  %ldexpv = call <2 x float> @air.ldexp.v2f32(<2 x float> %v, <2 x i32> %k)
  %lane0 = extractelement <2 x float> %ldexpv, i32 0
  %lane1 = extractelement <2 x float> %ldexpv, i32 1
  %rldexp = bitcast float %ldexp to i32
  %rlane0 = bitcast float %lane0 to i32
  %rlane1 = bitcast float %lane1 to i32
  %base = mul i32 %id, 3
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %rldexp, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %rlane0, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %rlane1, ptr addrspace(1) %p2, align 4
  ret void
}

define void @ldexp_f16(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %ta = trunc i32 %va to i16
  %x = bitcast i16 %ta to half
  %ldexp = call half @air.ldexp.f16(half %x, i32 %vb)
  %v0 = insertelement <2 x half> poison, half %x, i32 0
  %v = insertelement <2 x half> %v0, half %x, i32 1
  %k0 = insertelement <2 x i32> poison, i32 %vb, i32 0
  %kn = sub i32 0, %vb
  %k = insertelement <2 x i32> %k0, i32 %kn, i32 1
  %ldexpv = call <2 x half> @air.ldexp.v2f16(<2 x half> %v, <2 x i32> %k)
  %lane0 = extractelement <2 x half> %ldexpv, i32 0
  %lane1 = extractelement <2 x half> %ldexpv, i32 1
  %rldexph = bitcast half %ldexp to i16
  %rldexp = zext i16 %rldexph to i32
  %rlane0h = bitcast half %lane0 to i16
  %rlane0 = zext i16 %rlane0h to i32
  %rlane1h = bitcast half %lane1 to i16
  %rlane1 = zext i16 %rlane1h to i32
  %base = mul i32 %id, 3
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %rldexp, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %rlane0, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %rlane1, ptr addrspace(1) %p2, align 4
  ret void
}

define void @nextafter_f32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %x = bitcast i32 %va to float
  %y = bitcast i32 %vb to float
  %n = call float @air.nextafter.f32(float %x, float %y)
  %r = bitcast float %n to i32
  %base = mul i32 %id, 1
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %r, ptr addrspace(1) %p0, align 4
  ret void
}

define void @nextafter_f16(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %ta = trunc i32 %va to i16
  %x = bitcast i16 %ta to half
  %tb = trunc i32 %vb to i16
  %y = bitcast i16 %tb to half
  %n = call half @air.nextafter.f16(half %x, half %y)
  %rh = bitcast half %n to i16
  %r = zext i16 %rh to i32
  %base = mul i32 %id, 1
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %r, ptr addrspace(1) %p0, align 4
  ret void
}

define void @pi_f32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %x = bitcast i32 %va to float
  %sinpi = call float @air.sinpi.f32(float %x)
  %cospi = call float @air.cospi.f32(float %x)
  %tanpi = call float @air.tanpi.f32(float %x)
  %rsinpi = bitcast float %sinpi to i32
  %rcospi = bitcast float %cospi to i32
  %rtanpi = bitcast float %tanpi to i32
  %base = mul i32 %id, 3
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %rsinpi, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %rcospi, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %rtanpi, ptr addrspace(1) %p2, align 4
  ret void
}

define void @pi_f16(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %ta = trunc i32 %va to i16
  %x = bitcast i16 %ta to half
  %sinpi = call half @air.sinpi.f16(half %x)
  %cospi = call half @air.cospi.f16(half %x)
  %tanpi = call half @air.tanpi.f16(half %x)
  %rsinpih = bitcast half %sinpi to i16
  %rsinpi = zext i16 %rsinpih to i32
  %rcospih = bitcast half %cospi to i16
  %rcospi = zext i16 %rcospih to i32
  %rtanpih = bitcast half %tanpi to i16
  %rtanpi = zext i16 %rtanpih to i32
  %base = mul i32 %id, 3
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %rsinpi, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %rcospi, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %rtanpi, ptr addrspace(1) %p2, align 4
  ret void
}

define void @vector_v2f32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %x = bitcast i32 %va to float
  %y = bitcast i32 %vb to float
  %v0 = insertelement <2 x float> poison, float %x, i32 0
  %xh = fadd float %x, 0.5
  %v = insertelement <2 x float> %v0, float %xh, i32 1
  %cospi = call <2 x float> @air.cospi.v2f32(<2 x float> %v)
  %c0 = extractelement <2 x float> %cospi, i32 0
  %c1 = extractelement <2 x float> %cospi, i32 1
  %w0 = insertelement <2 x float> poison, float %y, i32 0
  %w = insertelement <2 x float> %w0, float %x, i32 1
  %cs = call <2 x float> @llvm.copysign.v2f32(<2 x float> %v, <2 x float> %w)
  %s0 = extractelement <2 x float> %cs, i32 0
  %s1 = extractelement <2 x float> %cs, i32 1
  %r0 = bitcast float %c0 to i32
  %r1 = bitcast float %c1 to i32
  %r2 = bitcast float %s0 to i32
  %r3 = bitcast float %s1 to i32
  %base = mul i32 %id, 4
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %r0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %r1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %r2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %r3, ptr addrspace(1) %p3, align 4
  ret void
}

declare float @air.asinh.f32(float)
declare float @air.acosh.f32(float)
declare float @air.atanh.f32(float)
declare float @air.exp10.f32(float)
declare float @air.log10.f32(float)
declare float @air.fabs.f32(float)
declare float @air.sinpi.f32(float)
declare float @air.cospi.f32(float)
declare float @air.tanpi.f32(float)
declare float @air.ldexp.f32(float, i32)
declare <2 x float> @air.ldexp.v2f32(<2 x float>, <2 x i32>)
declare float @air.nextafter.f32(float, float)
declare float @air.sincos.f32(float, ptr nocapture writeonly)
declare float @llvm.copysign.f32(float, float)
declare half @air.asinh.f16(half)
declare half @air.acosh.f16(half)
declare half @air.atanh.f16(half)
declare half @air.exp10.f16(half)
declare half @air.log10.f16(half)
declare half @air.fabs.f16(half)
declare half @air.sinpi.f16(half)
declare half @air.cospi.f16(half)
declare half @air.tanpi.f16(half)
declare half @air.ldexp.f16(half, i32)
declare <2 x half> @air.ldexp.v2f16(<2 x half>, <2 x i32>)
declare half @air.nextafter.f16(half, half)
declare half @air.sincos.f16(half, ptr nocapture writeonly)
declare half @llvm.copysign.f16(half, half)
declare <2 x float> @air.cospi.v2f32(<2 x float>)
declare <2 x float> @llvm.copysign.v2f32(<2 x float>, <2 x float>)

attributes #0 = { nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!10, !11, !12, !13, !14, !15, !16, !17, !18}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 2, i32 0}
!2 = !{!"Metal", i32 2, i32 2, i32 0}
!3 = !{}
!4 = !{!5, !6, !7}
!5 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}
!6 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"in"}
!7 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}
!10 = !{ptr @functions_f32, !3, !4}
!11 = !{ptr @functions_f16, !3, !4}
!12 = !{ptr @ldexp_f32, !3, !4}
!13 = !{ptr @ldexp_f16, !3, !4}
!14 = !{ptr @nextafter_f32, !3, !4}
!15 = !{ptr @nextafter_f16, !3, !4}
!16 = !{ptr @pi_f32, !3, !4}
!17 = !{ptr @pi_f16, !3, !4}
!18 = !{ptr @vector_v2f32, !3, !4}

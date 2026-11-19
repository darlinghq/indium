; Copyright (C) 2026 Lubos Dolezel
;
; Hand-written AIR for shadersrc/air-unpack.metal, in the form Metal's compiler gives such code: opaque pointers,
; buffers in address space 1 and the unpack functions as air.unpack.* intrinsics. See README.md.

source_filename = "air-unpack"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

; out[id * 16 + k]: unorm4x8 (0-3), snorm4x8 (4-7), unorm2x16 (8-9), snorm2x16 (10-11), unorm4x8 sRGB (12-15)
define void @unpack(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pin = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %packed = load i32, ptr addrspace(1) %pin, align 4
  %base = mul i32 %id, 16
  %a = call <4 x float> @air.unpack.unorm4x8.v4f32(i32 %packed)
  %b = call <4 x float> @air.unpack.snorm4x8.v4f32(i32 %packed)
  %c = call <2 x float> @air.unpack.unorm2x16.v2f32(i32 %packed)
  %d = call <2 x float> @air.unpack.snorm2x16.v2f32(i32 %packed)
  %e = call <4 x float> @air.unpack.unorm4x8.srgb.v4f32(i32 %packed)
  %a0 = extractelement <4 x float> %a, i32 0
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x0
  store float %a0, ptr addrspace(1) %p0, align 4
  %a1 = extractelement <4 x float> %a, i32 1
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x1
  store float %a1, ptr addrspace(1) %p1, align 4
  %a2 = extractelement <4 x float> %a, i32 2
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x2
  store float %a2, ptr addrspace(1) %p2, align 4
  %a3 = extractelement <4 x float> %a, i32 3
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x3
  store float %a3, ptr addrspace(1) %p3, align 4
  %b0 = extractelement <4 x float> %b, i32 0
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x4
  store float %b0, ptr addrspace(1) %p4, align 4
  %b1 = extractelement <4 x float> %b, i32 1
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x5
  store float %b1, ptr addrspace(1) %p5, align 4
  %b2 = extractelement <4 x float> %b, i32 2
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x6
  store float %b2, ptr addrspace(1) %p6, align 4
  %b3 = extractelement <4 x float> %b, i32 3
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x7
  store float %b3, ptr addrspace(1) %p7, align 4
  %c0 = extractelement <2 x float> %c, i32 0
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x8
  store float %c0, ptr addrspace(1) %p8, align 4
  %c1 = extractelement <2 x float> %c, i32 1
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x9
  store float %c1, ptr addrspace(1) %p9, align 4
  %d0 = extractelement <2 x float> %d, i32 0
  %i10 = add i32 %base, 10
  %x10 = zext i32 %i10 to i64
  %p10 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x10
  store float %d0, ptr addrspace(1) %p10, align 4
  %d1 = extractelement <2 x float> %d, i32 1
  %i11 = add i32 %base, 11
  %x11 = zext i32 %i11 to i64
  %p11 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x11
  store float %d1, ptr addrspace(1) %p11, align 4
  %e0 = extractelement <4 x float> %e, i32 0
  %i12 = add i32 %base, 12
  %x12 = zext i32 %i12 to i64
  %p12 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x12
  store float %e0, ptr addrspace(1) %p12, align 4
  %e1 = extractelement <4 x float> %e, i32 1
  %i13 = add i32 %base, 13
  %x13 = zext i32 %i13 to i64
  %p13 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x13
  store float %e1, ptr addrspace(1) %p13, align 4
  %e2 = extractelement <4 x float> %e, i32 2
  %i14 = add i32 %base, 14
  %x14 = zext i32 %i14 to i64
  %p14 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x14
  store float %e2, ptr addrspace(1) %p14, align 4
  %e3 = extractelement <4 x float> %e, i32 3
  %i15 = add i32 %base, 15
  %x15 = zext i32 %i15 to i64
  %p15 = getelementptr inbounds float, ptr addrspace(1) %out, i64 %x15
  store float %e3, ptr addrspace(1) %p15, align 4
  ret void
}

declare <4 x float> @air.unpack.unorm4x8.v4f32(i32)
declare <4 x float> @air.unpack.snorm4x8.v4f32(i32)
declare <2 x float> @air.unpack.unorm2x16.v2f32(i32)
declare <2 x float> @air.unpack.snorm2x16.v2f32(i32)
declare <4 x float> @air.unpack.unorm4x8.srgb.v4f32(i32)

attributes #0 = { norecurse nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!3}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 2, i32 0}
!2 = !{!"Metal", i32 2, i32 2, i32 0}

!3 = !{ptr @unpack, !4, !5}
!4 = !{}
!5 = !{!6, !7, !8}
!6 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"float", !"air.arg_name", !"out"}
!7 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"in"}
!8 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}

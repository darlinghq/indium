; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the fcmp-ordered test: fcmp ord and fcmp uno on float, half and a float vector, of two
; values, of a value with itself (an isnan test) and of a value with a constant. Thread id compares in[id] with
; in[(id + 8) % 16] and writes its results to out[id * n + k]; see fcmp-ordered.cpp for what each slot holds.
; See README.md for how this becomes fcmp-ordered.metallib.

source_filename = "fcmp-ordered"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @ordered_f32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %a = bitcast i32 %va to float
  %b = bitcast i32 %vb to float
  %ord = fcmp ord float %a, %b
  %uno = fcmp uno float %a, %b
  %ordself = fcmp ord float %a, %a
  %unoself = fcmp uno float %a, %a
  %ordconst = fcmp ord float %a, 0.0
  %unoconst = fcmp uno float 1.0, %b
  %rord = zext i1 %ord to i32
  %runo = zext i1 %uno to i32
  %rordself = zext i1 %ordself to i32
  %runoself = zext i1 %unoself to i32
  %rordconst = zext i1 %ordconst to i32
  %runoconst = zext i1 %unoconst to i32
  %base = mul i32 %id, 6
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %rord, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %runo, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %rordself, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %runoself, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %o4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o4
  store i32 %rordconst, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %o5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o5
  store i32 %runoconst, ptr addrspace(1) %p5, align 4
  ret void
}

define void @ordered_f16(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
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
  %a = bitcast i16 %ta to half
  %tb = trunc i32 %vb to i16
  %b = bitcast i16 %tb to half
  %ord = fcmp ord half %a, %b
  %uno = fcmp uno half %a, %b
  %ordself = fcmp ord half %a, %a
  %unoself = fcmp uno half %a, %a
  %ordconst = fcmp ord half %a, 0.0
  %unoconst = fcmp uno half 1.0, %b
  %rord = zext i1 %ord to i32
  %runo = zext i1 %uno to i32
  %rordself = zext i1 %ordself to i32
  %runoself = zext i1 %unoself to i32
  %rordconst = zext i1 %ordconst to i32
  %runoconst = zext i1 %unoconst to i32
  %base = mul i32 %id, 6
  %i0 = add i32 %base, 0
  %o0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o0
  store i32 %rord, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %o1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o1
  store i32 %runo, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %o2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o2
  store i32 %rordself, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %o3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o3
  store i32 %runoself, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %o4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o4
  store i32 %rordconst, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %o5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %o5
  store i32 %runoconst, ptr addrspace(1) %p5, align 4
  ret void
}

define void @ordered_v2f32(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pa = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %va = load i32, ptr addrspace(1) %pa, align 4
  %j0 = add i32 %id, 8
  %j = and i32 %j0, 15
  %jdx = zext i32 %j to i64
  %pb = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %jdx
  %vb = load i32, ptr addrspace(1) %pb, align 4
  %a = bitcast i32 %va to float
  %b = bitcast i32 %vb to float
  %v0 = insertelement <2 x float> poison, float %a, i32 0
  %v = insertelement <2 x float> %v0, float %b, i32 1
  %w0 = insertelement <2 x float> poison, float %b, i32 0
  %w = insertelement <2 x float> %w0, float 0x7FF8000000000000, i32 1
  %ord = fcmp ord <2 x float> %v, %w
  %uno = fcmp uno <2 x float> %v, %w
  %ord0 = extractelement <2 x i1> %ord, i32 0
  %ord1 = extractelement <2 x i1> %ord, i32 1
  %uno0 = extractelement <2 x i1> %uno, i32 0
  %uno1 = extractelement <2 x i1> %uno, i32 1
  %r0 = zext i1 %ord0 to i32
  %r1 = zext i1 %ord1 to i32
  %r2 = zext i1 %uno0 to i32
  %r3 = zext i1 %uno1 to i32
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


attributes #0 = { nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!10, !11, !12}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 2, i32 0}
!2 = !{!"Metal", i32 2, i32 2, i32 0}
!3 = !{}
!4 = !{!5, !6, !7}
!5 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}
!6 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"in"}
!7 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}
!10 = !{ptr @ordered_f32, !3, !4}
!11 = !{ptr @ordered_f16, !3, !4}
!12 = !{ptr @ordered_v2f32, !3, !4}

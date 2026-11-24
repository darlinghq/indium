; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the simd-relative-shuffles test: simd_shuffle_down and simd_shuffle_up, which leave the
; lanes with no lane to read from alone. One threadgroup runs each kernel; thread t shuffles its own values of t (see
; simd-relative-shuffles.cpp) and writes the results to out[t * n + k]. See README.md for how this becomes
; simd-relative-shuffles.metallib.

source_filename = "simd-relative-shuffles"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @shuffle_up_down(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %w16 = trunc i32 %width to i16
  %m = mul i32 %t, 7
  %vi = add i32 %m, 1000
  %fi = add i32 %m, 65536
  %v = add i32 %vi, 0
  %f = add i32 %fi, 0
  %r0 = call i32 @air.simd_shuffle_down.s.i32(i32 %v, i16 1) #1
  %o0 = add i32 %r0, 0
  %r1 = call i32 @air.simd_shuffle_down.s.i32(i32 %v, i16 3) #1
  %o1 = add i32 %r1, 0
  %r2 = call i32 @air.simd_shuffle_up.s.i32(i32 %v, i16 1) #1
  %o2 = add i32 %r2, 0
  %r3 = call i32 @air.simd_shuffle_up.s.i32(i32 %v, i16 3) #1
  %o3 = add i32 %r3, 0
  %base = mul i32 %t, 5
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
  store i32 %width, ptr addrspace(1) %p4, align 4
  ret void
}

define void @shuffle_up_down_float(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %w16 = trunc i32 %width to i16
  %m = mul i32 %t, 7
  %vi = add i32 %m, 1000
  %fi = add i32 %m, 65536
  %v = uitofp i32 %vi to float
  %f = uitofp i32 %fi to float
  %r0 = call float @air.simd_shuffle_down.f32(float %v, i16 1) #1
  %o0 = fptoui float %r0 to i32
  %r1 = call float @air.simd_shuffle_down.f32(float %v, i16 3) #1
  %o1 = fptoui float %r1 to i32
  %r2 = call float @air.simd_shuffle_up.f32(float %v, i16 1) #1
  %o2 = fptoui float %r2 to i32
  %r3 = call float @air.simd_shuffle_up.f32(float %v, i16 3) #1
  %o3 = fptoui float %r3 to i32
  %base = mul i32 %t, 5
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
  store i32 %width, ptr addrspace(1) %p4, align 4
  ret void
}

declare i32 @air.simd_shuffle_down.s.i32(i32, i16) local_unnamed_addr #1
declare i32 @air.simd_shuffle_up.s.i32(i32, i16) local_unnamed_addr #1
declare float @air.simd_shuffle_down.f32(float, i16) local_unnamed_addr #1
declare float @air.simd_shuffle_up.f32(float, i16) local_unnamed_addr #1

attributes #0 = { nounwind }
attributes #1 = { convergent nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!10, !11}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 4, i32 0}
!2 = !{!"Metal", i32 2, i32 4, i32 0}
!3 = !{}
!4 = !{!5, !6, !7, !8}
!5 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!6 = !{i32 1, !"air.thread_position_in_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"t"}
!7 = !{i32 2, !"air.thread_index_in_simdgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"lane"}
!8 = !{i32 3, !"air.threads_per_simdgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"width"}
!10 = !{ptr @shuffle_up_down, !3, !4}
!11 = !{ptr @shuffle_up_down_float, !3, !4}


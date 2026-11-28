; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the kernel-input-attributes test: kernels whose parameters are the quad-group
; built-ins and the dispatch_ variants of the threadgroup's sizes, as 32-bit and 16-bit integers. Thread t writes them to out[t * n + k]. See README.md for how this becomes
; kernel-input-attributes.metallib.

source_filename = "kernel-input-attributes"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @quad_ids(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %thread_index_in_quadgroup, i32 %quadgroup_index_in_threadgroup, i32 %quadgroups_per_threadgroup, i32 %dispatch_quadgroups_per_threadgroup, i32 %dispatch_simdgroups_per_threadgroup, i32 %threads_per_simdgroup) local_unnamed_addr #0 {
entry:
  %base = mul i32 %t, 6
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %thread_index_in_quadgroup, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %quadgroup_index_in_threadgroup, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %quadgroups_per_threadgroup, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %dispatch_quadgroups_per_threadgroup, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %dispatch_simdgroups_per_threadgroup, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %threads_per_simdgroup, ptr addrspace(1) %p5, align 4
  ret void
}

define void @quad_ids_short(ptr addrspace(1) noalias nocapture %out, i16 %t16, i16 %thread_index_in_quadgroup, i16 %quadgroup_index_in_threadgroup, i16 %quadgroups_per_threadgroup, i16 %dispatch_quadgroups_per_threadgroup, i16 %dispatch_simdgroups_per_threadgroup, i16 %threads_per_simdgroup) local_unnamed_addr #0 {
entry:
  %odd = icmp ugt i16 %t16, 7
  %t16s = select i1 %odd, i16 %t16, i16 %t16
  %t = zext i16 %t16s to i32
  %thread_index_in_quadgroup_s = select i1 %odd, i16 %thread_index_in_quadgroup, i16 %thread_index_in_quadgroup
  %thread_index_in_quadgroup_ = zext i16 %thread_index_in_quadgroup_s to i32
  %quadgroup_index_in_threadgroup_s = select i1 %odd, i16 %quadgroup_index_in_threadgroup, i16 %quadgroup_index_in_threadgroup
  %quadgroup_index_in_threadgroup_ = zext i16 %quadgroup_index_in_threadgroup_s to i32
  %quadgroups_per_threadgroup_s = select i1 %odd, i16 %quadgroups_per_threadgroup, i16 %quadgroups_per_threadgroup
  %quadgroups_per_threadgroup_ = zext i16 %quadgroups_per_threadgroup_s to i32
  %dispatch_quadgroups_per_threadgroup_s = select i1 %odd, i16 %dispatch_quadgroups_per_threadgroup, i16 %dispatch_quadgroups_per_threadgroup
  %dispatch_quadgroups_per_threadgroup_ = zext i16 %dispatch_quadgroups_per_threadgroup_s to i32
  %dispatch_simdgroups_per_threadgroup_s = select i1 %odd, i16 %dispatch_simdgroups_per_threadgroup, i16 %dispatch_simdgroups_per_threadgroup
  %dispatch_simdgroups_per_threadgroup_ = zext i16 %dispatch_simdgroups_per_threadgroup_s to i32
  %threads_per_simdgroup_s = select i1 %odd, i16 %threads_per_simdgroup, i16 %threads_per_simdgroup
  %threads_per_simdgroup_ = zext i16 %threads_per_simdgroup_s to i32
  %base = mul i32 %t, 6
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %thread_index_in_quadgroup_, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %quadgroup_index_in_threadgroup_, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %quadgroups_per_threadgroup_, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %dispatch_quadgroups_per_threadgroup_, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %dispatch_simdgroups_per_threadgroup_, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %threads_per_simdgroup_, ptr addrspace(1) %p5, align 4
  ret void
}

define void @grid_sizes(ptr addrspace(1) noalias nocapture %out, i32 %t, <3 x i32> %dispatch, i32 %width) local_unnamed_addr #0 {
entry:
  %dispatch0 = extractelement <3 x i32> %dispatch, i64 0
  %dispatch1 = extractelement <3 x i32> %dispatch, i64 1
  %dispatch2 = extractelement <3 x i32> %dispatch, i64 2
  %base = mul i32 %t, 4
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %dispatch0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %dispatch1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %dispatch2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %width, ptr addrspace(1) %p3, align 4
  ret void
}

define void @grid_sizes_short(ptr addrspace(1) noalias nocapture %out, i16 %t16, i16 %dispatch, i16 %width16) local_unnamed_addr #0 {
entry:
  %t = zext i16 %t16 to i32
  %width = zext i16 %width16 to i32
  %dispatch0 = zext i16 %dispatch to i32
  %base = mul i32 %t, 2
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %dispatch0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %width, ptr addrspace(1) %p1, align 4
  ret void
}

attributes #0 = { nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!1000, !1001, !1002, !1003}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 4, i32 0}
!2 = !{!"Metal", i32 2, i32 4, i32 0}
!3 = !{}
!10 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!11 = !{i32 1, !"air.thread_index_in_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"t"}
!12 = !{i32 2, !"air.thread_index_in_quadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"thread_index_in_quadgroup"}
!13 = !{i32 3, !"air.quadgroup_index_in_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"quadgroup_index_in_threadgroup"}
!14 = !{i32 4, !"air.quadgroups_per_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"quadgroups_per_threadgroup"}
!15 = !{i32 5, !"air.dispatch_quadgroups_per_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"dispatch_quadgroups_per_threadgroup"}
!16 = !{i32 6, !"air.dispatch_simdgroups_per_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"dispatch_simdgroups_per_threadgroup"}
!17 = !{i32 7, !"air.threads_per_simdgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"threads_per_simdgroup"}
!18 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!19 = !{i32 1, !"air.thread_index_in_threadgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"t16"}
!20 = !{i32 2, !"air.thread_index_in_quadgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"thread_index_in_quadgroup"}
!21 = !{i32 3, !"air.quadgroup_index_in_threadgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"quadgroup_index_in_threadgroup"}
!22 = !{i32 4, !"air.quadgroups_per_threadgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"quadgroups_per_threadgroup"}
!23 = !{i32 5, !"air.dispatch_quadgroups_per_threadgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"dispatch_quadgroups_per_threadgroup"}
!24 = !{i32 6, !"air.dispatch_simdgroups_per_threadgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"dispatch_simdgroups_per_threadgroup"}
!25 = !{i32 7, !"air.threads_per_simdgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"threads_per_simdgroup"}
!26 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!27 = !{i32 1, !"air.thread_index_in_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"t"}
!28 = !{i32 2, !"air.dispatch_threads_per_threadgroup", !"air.arg_type_name", !"uint3", !"air.arg_name", !"dispatch"}
!29 = !{i32 3, !"air.threads_per_simdgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"width"}
!30 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!31 = !{i32 1, !"air.thread_index_in_threadgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"t16"}
!32 = !{i32 2, !"air.dispatch_threads_per_threadgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"dispatch"}
!33 = !{i32 3, !"air.threads_per_simdgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"width16"}
!2000 = !{!10, !11, !12, !13, !14, !15, !16, !17}
!1000 = !{ptr @quad_ids, !3, !2000}
!2001 = !{!18, !19, !20, !21, !22, !23, !24, !25}
!1001 = !{ptr @quad_ids_short, !3, !2001}
!2002 = !{!26, !27, !28, !29}
!1002 = !{ptr @grid_sizes, !3, !2002}
!2003 = !{!30, !31, !32, !33}
!1003 = !{ptr @grid_sizes_short, !3, !2003}

; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the stage-in-region test: kernels whose parameters are the stage-in region, MSL's
; [[grid_origin]] and [[grid_size]] (AIR's stage_in_grid_origin and stage_in_grid_size), as 3 and 2-component
; vectors and a scalar. Thread t writes them to out[t * n + k]. See README.md for how this becomes
; stage-in-region.metallib.

source_filename = "stage-in-region"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @region(ptr addrspace(1) noalias nocapture %out, i32 %t, <3 x i32> %origin, <3 x i32> %size) local_unnamed_addr #0 {
entry:
  %origin0 = extractelement <3 x i32> %origin, i64 0
  %origin1 = extractelement <3 x i32> %origin, i64 1
  %origin2 = extractelement <3 x i32> %origin, i64 2
  %size0 = extractelement <3 x i32> %size, i64 0
  %size1 = extractelement <3 x i32> %size, i64 1
  %size2 = extractelement <3 x i32> %size, i64 2
  %base = mul i32 %t, 6
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %origin0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %origin1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %origin2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %size0, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %size1, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %size2, ptr addrspace(1) %p5, align 4
  ret void
}

define void @region_2d(ptr addrspace(1) noalias nocapture %out, i32 %t, <2 x i32> %origin, <2 x i32> %size) local_unnamed_addr #0 {
entry:
  %origin0 = extractelement <2 x i32> %origin, i64 0
  %origin1 = extractelement <2 x i32> %origin, i64 1
  %size0 = extractelement <2 x i32> %size, i64 0
  %size1 = extractelement <2 x i32> %size, i64 1
  %base = mul i32 %t, 4
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %origin0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %origin1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %size0, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %size1, ptr addrspace(1) %p3, align 4
  ret void
}

define void @region_1d(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %origin, i32 %size) local_unnamed_addr #0 {
entry:
  %base = mul i32 %t, 2
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %origin, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %size, ptr addrspace(1) %p1, align 4
  ret void
}

attributes #0 = { nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!1000, !1001, !1002}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 4, i32 0}
!2 = !{!"Metal", i32 2, i32 4, i32 0}
!3 = !{}
!10 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!11 = !{i32 1, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"t"}
!12 = !{i32 2, !"air.stage_in_grid_origin", !"air.arg_type_name", !"uint3", !"air.arg_name", !"origin"}
!13 = !{i32 3, !"air.stage_in_grid_size", !"air.arg_type_name", !"uint3", !"air.arg_name", !"size"}
!14 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!15 = !{i32 1, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"t"}
!16 = !{i32 2, !"air.stage_in_grid_origin", !"air.arg_type_name", !"uint2", !"air.arg_name", !"origin"}
!17 = !{i32 3, !"air.stage_in_grid_size", !"air.arg_type_name", !"uint2", !"air.arg_name", !"size"}
!18 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!19 = !{i32 1, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"t"}
!20 = !{i32 2, !"air.stage_in_grid_origin", !"air.arg_type_name", !"uint", !"air.arg_name", !"origin"}
!21 = !{i32 3, !"air.stage_in_grid_size", !"air.arg_type_name", !"uint", !"air.arg_name", !"size"}
!2000 = !{!10, !11, !12, !13}
!1000 = !{ptr @region, !3, !2000}
!2001 = !{!14, !15, !16, !17}
!1001 = !{ptr @region_2d, !3, !2001}
!2002 = !{!18, !19, !20, !21}
!1002 = !{ptr @region_1d, !3, !2002}

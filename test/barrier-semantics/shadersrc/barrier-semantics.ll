; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the barrier-semantics test: threadgroup and simdgroup barriers and fences that order
; no memory, or only memory Vulkan doesn't have, and fences of every scope. Thread t stores 1, 2, ... to
; out[t * n + k] between them. See README.md for how this becomes barrier-semantics.metallib.

source_filename = "barrier-semantics"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @barriers(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %base = mul i32 %t, 8
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 1, ptr addrspace(1) %p0, align 4
  call void @air.wg.barrier(i32 0, i32 1) #1
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 2, ptr addrspace(1) %p1, align 4
  call void @air.wg.barrier(i32 8, i32 1) #1
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 3, ptr addrspace(1) %p2, align 4
  call void @air.wg.barrier(i32 2, i32 1) #1
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 4, ptr addrspace(1) %p3, align 4
  call void @air.simdgroup.barrier(i32 0, i32 4) #1
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 5, ptr addrspace(1) %p4, align 4
  call void @air.simdgroup.barrier(i32 8, i32 4) #1
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 6, ptr addrspace(1) %p5, align 4
  call void @air.simdgroup.barrier(i32 3, i32 4) #1
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 7, ptr addrspace(1) %p6, align 4
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %width, ptr addrspace(1) %p7, align 4
  ret void
}

define void @fences(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %base = mul i32 %t, 7
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 1, ptr addrspace(1) %p0, align 4
  call void @air.atomic.fence(i32 0, i32 5, i32 2) #1
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 2, ptr addrspace(1) %p1, align 4
  call void @air.atomic.fence(i32 8, i32 4, i32 1) #1
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 3, ptr addrspace(1) %p2, align 4
  call void @air.atomic.fence(i32 3, i32 0, i32 2) #1
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 4, ptr addrspace(1) %p3, align 4
  call void @air.atomic.fence(i32 1, i32 3, i32 2) #1
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 5, ptr addrspace(1) %p4, align 4
  call void @air.atomic.fence(i32 2, i32 2, i32 1) #1
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 6, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %width, ptr addrspace(1) %p6, align 4
  ret void
}

define void @fence_scopes(ptr addrspace(1) noalias nocapture %out, i32 %t, i32 %lane, i32 %width) local_unnamed_addr #0 {
entry:
  %base = mul i32 %t, 6
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 1, ptr addrspace(1) %p0, align 4
  call void @air.atomic.fence(i32 3, i32 5, i32 0) #1
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 2, ptr addrspace(1) %p1, align 4
  call void @air.atomic.fence(i32 5, i32 0, i32 4) #1
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 3, ptr addrspace(1) %p2, align 4
  call void @air.atomic.fence(i32 2, i32 4, i32 4) #1
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 4, ptr addrspace(1) %p3, align 4
  call void @air.atomic.fence(i32 1, i32 5, i32 4) #1
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 5, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %width, ptr addrspace(1) %p5, align 4
  ret void
}

declare void @air.wg.barrier(i32, i32) local_unnamed_addr #1
declare void @air.simdgroup.barrier(i32, i32) local_unnamed_addr #1
declare void @air.atomic.fence(i32, i32, i32) local_unnamed_addr #1

attributes #0 = { nounwind }
attributes #1 = { convergent nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!100, !101, !102}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 4, i32 0}
!2 = !{!"Metal", i32 2, i32 4, i32 0}
!3 = !{}
!4 = !{!5, !6, !7, !8}
!5 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!6 = !{i32 1, !"air.thread_position_in_threadgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"t"}
!7 = !{i32 2, !"air.thread_index_in_simdgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"lane"}
!8 = !{i32 3, !"air.threads_per_simdgroup", !"air.arg_type_name", !"uint", !"air.arg_name", !"width"}
!100 = !{ptr @barriers, !3, !4}
!101 = !{ptr @fences, !3, !4}
!102 = !{ptr @fence_scopes, !3, !4}


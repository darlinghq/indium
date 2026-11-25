; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the kernels in pipeline-limits.metal. A kernel's threadgroup size limit is an
; operand of its air.kernel node after the parameters: !{!"air.max_work_group_size", i32 N}. See
; README.md for how this becomes pipeline-limits.metallib.

source_filename = "pipeline-limits"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @unlimited(ptr addrspace(1) noalias nocapture %out, <2 x i32> %id, <2 x i32> %size) local_unnamed_addr #0 {
entry:
  %x = extractelement <2 x i32> %id, i64 0
  %y = extractelement <2 x i32> %id, i64 1
  %width = extractelement <2 x i32> %size, i64 0
  %row = mul i32 %y, %width
  %i = add i32 %row, %x
  %idx = zext i32 %i to i64
  %p = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %idx
  store i32 %i, ptr addrspace(1) %p, align 4
  ret void
}

define void @limited(ptr addrspace(1) noalias nocapture %out, <2 x i32> %id, <2 x i32> %size) local_unnamed_addr #0 {
entry:
  %x = extractelement <2 x i32> %id, i64 0
  %y = extractelement <2 x i32> %id, i64 1
  %width = extractelement <2 x i32> %size, i64 0
  %row = mul i32 %y, %width
  %i = add i32 %row, %x
  %idx = zext i32 %i to i64
  %p = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %idx
  store i32 %i, ptr addrspace(1) %p, align 4
  ret void
}

define void @too_large(ptr addrspace(1) noalias nocapture %out, <2 x i32> %id, <2 x i32> %size) local_unnamed_addr #0 {
entry:
  %x = extractelement <2 x i32> %id, i64 0
  %y = extractelement <2 x i32> %id, i64 1
  %width = extractelement <2 x i32> %size, i64 0
  %row = mul i32 %y, %width
  %i = add i32 %row, %x
  %idx = zext i32 %i to i64
  %p = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %idx
  store i32 %i, ptr addrspace(1) %p, align 4
  ret void
}

attributes #0 = { norecurse nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!3, !8, !11}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 2, i32 0}
!2 = !{!"Metal", i32 2, i32 2, i32 0}

!3 = !{ptr @unlimited, !4, !5}
!4 = !{}
!5 = !{!6, !7, !10}
!6 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"uint", !"air.arg_name", !"out"}
!7 = !{i32 1, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint2", !"air.arg_name", !"id"}
!10 = !{i32 2, !"air.threads_per_grid", !"air.arg_type_name", !"uint2", !"air.arg_name", !"size"}

!8 = !{ptr @limited, !4, !5, !9}
!9 = !{!"air.max_work_group_size", i32 64}

!11 = !{ptr @too_large, !4, !5, !12}
!12 = !{!"air.max_work_group_size", i32 2048}

; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the narrow-builtins test: a kernel whose thread position and index parameters are
; 16-bit integers (MSL's ushort and ushort2/3), which Iridium narrows from the 32-bit SPIR-V builtins.
; Thread x writes its results to out[x * 7 + k]; see narrow-builtins.cpp for what each slot holds. See
; README.md for how this becomes narrow-builtins.metallib.

source_filename = "narrow-builtins"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @narrow_builtins(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, <3 x i16> %position, i16 %index, <2 x i16> %group) local_unnamed_addr #0 {
entry:
  %x = extractelement <3 x i16> %position, i64 0
  %r0 = zext i16 %x to i32
  %y = extractelement <3 x i16> %position, i64 1
  %r1 = zext i16 %y to i32
  %r2 = zext i16 %index to i32
  %low = icmp ult i16 %index, 3
  %picked = select i1 %low, i16 %index, i16 100
  %r3 = zext i16 %picked to i32
  %gx = extractelement <2 x i16> %group, i64 0
  %r4 = zext i16 %gx to i32
  %gy = extractelement <2 x i16> %group, i64 1
  %r5 = zext i16 %gy to i32
  %next = add <3 x i16> %position, <i16 1, i16 1, i16 1>
  %nx = extractelement <3 x i16> %next, i64 0
  %r6 = zext i16 %nx to i32
  %id = zext i16 %x to i32
  %base = mul i32 %id, 7
  %i0 = add i32 %base, 0
  %x0 = zext i32 %i0 to i64
  %p0 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x0
  store i32 %r0, ptr addrspace(1) %p0, align 4
  %i1 = add i32 %base, 1
  %x1 = zext i32 %i1 to i64
  %p1 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x1
  store i32 %r1, ptr addrspace(1) %p1, align 4
  %i2 = add i32 %base, 2
  %x2 = zext i32 %i2 to i64
  %p2 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x2
  store i32 %r2, ptr addrspace(1) %p2, align 4
  %i3 = add i32 %base, 3
  %x3 = zext i32 %i3 to i64
  %p3 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x3
  store i32 %r3, ptr addrspace(1) %p3, align 4
  %i4 = add i32 %base, 4
  %x4 = zext i32 %i4 to i64
  %p4 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x4
  store i32 %r4, ptr addrspace(1) %p4, align 4
  %i5 = add i32 %base, 5
  %x5 = zext i32 %i5 to i64
  %p5 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x5
  store i32 %r5, ptr addrspace(1) %p5, align 4
  %i6 = add i32 %base, 6
  %x6 = zext i32 %i6 to i64
  %p6 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x6
  store i32 %r6, ptr addrspace(1) %p6, align 4
  ret void
}

attributes #0 = { nounwind }

!llvm.module.flags = !{!0}
!air.version = !{!1}
!air.language_version = !{!2}
!air.kernel = !{!3}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 2, i32 2, i32 0}
!2 = !{!"Metal", i32 2, i32 2, i32 0}

!3 = !{ptr @narrow_builtins, !4, !5}
!4 = !{}
!5 = !{!6, !7, !8, !9, !10}
!6 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}
!7 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"in"}
!8 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"ushort3", !"air.arg_name", !"position"}
!9 = !{i32 3, !"air.thread_index_in_threadgroup", !"air.arg_type_name", !"ushort", !"air.arg_name", !"index"}
!10 = !{i32 4, !"air.threadgroup_position_in_grid", !"air.arg_type_name", !"ushort2", !"air.arg_name", !"group"}

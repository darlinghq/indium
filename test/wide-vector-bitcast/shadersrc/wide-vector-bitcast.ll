; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the wide-vector-bitcast test: bitcasts to vectors of more than four elements, which
; SPIR-V cannot hold without the Vector16 capability, whose elements are then extracted. Each thread reads
; in[id] and writes its results to out[id * 10 + k]; see wide-vector-bitcast.cpp for what each slot holds.
; See README.md for how this becomes wide-vector-bitcast.metallib.

source_filename = "wide-vector-bitcast"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

define void @wide_vector_bitcast(ptr addrspace(1) noalias nocapture %out, ptr addrspace(1) noalias nocapture readonly %in, i32 %id) local_unnamed_addr #0 {
entry:
  %idx = zext i32 %id to i64
  %pv = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %idx
  %v = load i32, ptr addrspace(1) %pv, align 4
  ; w = (v, v * 3, ~v)
  %v3 = mul i32 %v, 3
  %vn = xor i32 %v, -1
  %w0 = insertelement <3 x i32> undef, i32 %v, i64 0
  %w1 = insertelement <3 x i32> %w0, i32 %v3, i64 1
  %w = insertelement <3 x i32> %w1, i32 %vn, i64 2
  ; bytes 0, 5 and 11 of w, byte id * 5 % 12, and byte 1 or 0 by whether v is negative
  %bytes = bitcast <3 x i32> %w to <12 x i8>
  %b0 = extractelement <12 x i8> %bytes, i64 0
  %r0 = zext i8 %b0 to i32
  %b5 = extractelement <12 x i8> %bytes, i64 5
  %r1 = zext i8 %b5 to i32
  %b11 = extractelement <12 x i8> %bytes, i32 11
  %r2 = zext i8 %b11 to i32
  %idx5 = mul i64 %idx, 5
  %dyn = urem i64 %idx5, 12
  %bd = extractelement <12 x i8> %bytes, i64 %dyn
  %r3 = zext i8 %bd to i32
  %neg = icmp slt i32 %v, 0
  %bn = extractelement <12 x i8> %bytes, i1 %neg
  %r9 = zext i8 %bn to i32
  ; an index past the end gives poison: translated, but not stored
  %past = extractelement <12 x i8> %bytes, i64 12
  ; half-words 3 and id % 6 of w
  %halves = bitcast <3 x i32> %w to <6 x i16>
  %h3 = extractelement <6 x i16> %halves, i64 3
  %r4 = zext i16 %h3 to i32
  %dyn6 = urem i32 %id, 6
  %hd = extractelement <6 x i16> %halves, i32 %dyn6
  %r5 = sext i16 %hd to i32
  ; byte 6 of the 64-bit integer v * 2^32 + v * 3
  %z = zext i32 %v to i64
  %zh = shl i64 %z, 32
  %z3 = zext i32 %v3 to i64
  %q = or i64 %zh, %z3
  %qbytes = bitcast i64 %q to <8 x i8>
  %q6 = extractelement <8 x i8> %qbytes, i64 6
  %r6 = zext i8 %q6 to i32
  ; byte 4 of the float vector with the bits of (v, ~v), and the same as a half from <4 x half>
  %fv = bitcast i32 %v to float
  %fn = bitcast i32 %vn to float
  %f0 = insertelement <2 x float> undef, float %fv, i64 0
  %f = insertelement <2 x float> %f0, float %fn, i64 1
  %fbytes = bitcast <2 x float> %f to <8 x i8>
  %fb4 = extractelement <8 x i8> %fbytes, i64 4
  %r7 = zext i8 %fb4 to i32
  %wide = bitcast <3 x i32> %w to <6 x half>
  %wh = extractelement <6 x half> %wide, i64 1
  %whi = bitcast half %wh to i16
  %r8 = zext i16 %whi to i32
  %base = mul i32 %id, 10
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
  %i7 = add i32 %base, 7
  %x7 = zext i32 %i7 to i64
  %p7 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x7
  store i32 %r7, ptr addrspace(1) %p7, align 4
  %i8 = add i32 %base, 8
  %x8 = zext i32 %i8 to i64
  %p8 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x8
  store i32 %r8, ptr addrspace(1) %p8, align 4
  %i9 = add i32 %base, 9
  %x9 = zext i32 %i9 to i64
  %p9 = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %x9
  store i32 %r9, ptr addrspace(1) %p9, align 4
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

!3 = !{ptr @wide_vector_bitcast, !4, !5}
!4 = !{}
!5 = !{!6, !7, !10}
!6 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}
!7 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"in"}
!10 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}

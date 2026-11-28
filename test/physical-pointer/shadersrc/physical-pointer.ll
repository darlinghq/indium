; Copyright (C) 2026 Darling Developers
;
; Hand-written AIR for the kernels in physical-pointer.cpp: device and constant pointers whose pointee
; the module doesn't say. Every kernel writes one int per thread to buffer 0.
; See README.md for how this becomes physical-pointer.metallib.

source_filename = "physical-pointer"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

; struct { device void* data; int scale; }, the one struct type the module declares
%struct.Array = type { ptr addrspace(1), i32 }

; struct Params { int scale; float bias; }, named only in air.arg_type_name:
; out[id] = params.scale * id + int(params.bias)
define void @untyped_argument(ptr addrspace(1) %out, ptr addrspace(1) %params, i32 %id) #0 {
  %scale = load i32, ptr addrspace(1) %params, align 4
  %biasp = getelementptr inbounds i8, ptr addrspace(1) %params, i64 4
  %bias = load float, ptr addrspace(1) %biasp, align 4
  %biasi = fptosi float %bias to i32
  %scaled = mul i32 %scale, %id
  %v = add i32 %scaled, %biasi
  %i = zext i32 %id to i64
  %o = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %i
  store i32 %v, ptr addrspace(1) %o, align 4
  ret void
}

; array.data holds 32 ints followed by 32 floats:
; out[id] = (((int*)array.data)[id] + int(((float*)array.data)[32 + id])) * array.scale
define void @loaded_pointer(ptr addrspace(1) %out, ptr addrspace(1) %array, i32 %id) #0 {
  %data = load ptr addrspace(1), ptr addrspace(1) %array, align 8
  %scalep = getelementptr inbounds %struct.Array, ptr addrspace(1) %array, i64 0, i32 1
  %scale = load i32, ptr addrspace(1) %scalep, align 8
  %i = zext i32 %id to i64
  %ip = getelementptr inbounds i32, ptr addrspace(1) %data, i64 %i
  %x = load i32, ptr addrspace(1) %ip, align 4
  %floats = getelementptr inbounds i8, ptr addrspace(1) %data, i64 128
  %fp = getelementptr inbounds float, ptr addrspace(1) %floats, i64 %i
  %f = load float, ptr addrspace(1) %fp, align 4
  %fi = fptosi float %f to i32
  %sum = add i32 %x, %fi
  %v = mul i32 %sum, %scale
  %o = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %i
  store i32 %v, ptr addrspace(1) %o, align 4
  ret void
}

; constant struct Args { device int* data; }& args: out[id] = args.data[id]
define void @constant_argument(ptr addrspace(1) %out, ptr addrspace(2) %args, i32 %id) #0 {
  %data = load ptr addrspace(1), ptr addrspace(2) %args, align 8
  %i = zext i32 %id to i64
  %p = getelementptr inbounds i32, ptr addrspace(1) %data, i64 %i
  %v = load i32, ptr addrspace(1) %p, align 4
  %o = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %i
  store i32 %v, ptr addrspace(1) %o, align 4
  ret void
}

; each thread stores the address of in[id] to slots[id] and reads in[id] back through it:
; out[id] = in[id]
define void @stored_pointer(ptr addrspace(1) %out, ptr addrspace(1) %in, ptr addrspace(1) %slots, i32 %id) #0 {
  %i = zext i32 %id to i64
  %p = getelementptr inbounds i32, ptr addrspace(1) %in, i64 %i
  %slot = getelementptr inbounds ptr addrspace(1), ptr addrspace(1) %slots, i64 %i
  store ptr addrspace(1) %p, ptr addrspace(1) %slot, align 8
  %q = load ptr addrspace(1), ptr addrspace(1) %slot, align 8
  %v = load i32, ptr addrspace(1) %q, align 4
  %o = getelementptr inbounds i32, ptr addrspace(1) %out, i64 %i
  store i32 %v, ptr addrspace(1) %o, align 4
  ret void
}

attributes #0 = { nounwind }

!air.version = !{!0}
!air.language_version = !{!1}
!air.kernel = !{!10, !20, !30, !40}

!0 = !{i32 2, i32 2, i32 0}
!1 = !{!"Metal", i32 2, i32 2, i32 0}
!2 = !{}
!3 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"out"}

!10 = !{ptr @untyped_argument, !2, !11}
!11 = !{!3, !12, !13}
!12 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"Params", !"air.arg_name", !"params"}
!13 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}

!20 = !{ptr @loaded_pointer, !2, !21}
!21 = !{!3, !22, !13}
!22 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 8, !"air.arg_type_name", !"Array", !"air.arg_name", !"array"}

!30 = !{ptr @constant_argument, !2, !31}
!31 = !{!3, !32, !13}
!32 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 8, !"air.arg_type_name", !"Args", !"air.arg_name", !"args"}

!40 = !{ptr @stored_pointer, !2, !41}
!41 = !{!3, !42, !43, !44}
!42 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 4, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"int", !"air.arg_name", !"in"}
!43 = !{i32 2, !"air.buffer", !"air.location_index", i32 2, i32 1, !"air.read_write", !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 8, !"air.arg_type_name", !"Slot", !"air.arg_name", !"slots"}
!44 = !{i32 3, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"id"}

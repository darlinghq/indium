; ModuleID = 'write_columns'
source_filename = "write_columns"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

; Hand-written AIR for shadersrc/store-reference.metal, in the shape metalfe
; emits. See test/store-reference/README.md for why this is checked in rather
; than compiled, and for how to turn it back into a .metallib. The
; struct.metal::matrix column idiom (getelementptr into a matrix, then a
; "bitcast ptr to ptr" re-typing the address as a 16-byte padded column) is
; copied from test/lighting's vertex_project, where it only appears on the load
; side.
%"struct.metal::matrix.0" = type { [3 x <3 x float>] }
%struct.Uniforms = type { <4 x float>, %"struct.metal::matrix.0" }

; Function Attrs: norecurse nounwind memory(readwrite)
define void @write_columns(ptr addrspace(1) noalias captures(none) %0, ptr addrspace(2) noalias readonly captures(none) dereferenceable(64) %1) local_unnamed_addr #0 {
entry:
  ; dst.color = src.color. Both getelementptrs descend to <4 x float>, so the
  ; store's pointer and value agree and this store is well-typed.
  %3 = getelementptr inbounds %struct.Uniforms, ptr addrspace(2) %1, i64 0, i32 0
  %4 = load <4 x float>, ptr addrspace(2) %3, align 16
  %5 = getelementptr inbounds %struct.Uniforms, ptr addrspace(1) %0, i64 0, i32 0
  store <4 x float> %4, ptr addrspace(1) %5, align 16

  ; dst.basis[0] = src.basis[0]. These stop at the matrix aggregate, the shape
  ; test/lighting's %43/%46/%47 load has, so the pointer names the matrix and
  ; not the column being written.
  %6 = getelementptr inbounds %struct.Uniforms, ptr addrspace(2) %1, i64 0, i32 1
  %7 = bitcast ptr addrspace(2) %6 to ptr addrspace(2)
  %8 = load <4 x float>, ptr addrspace(2) %7, align 16
  %9 = getelementptr inbounds %struct.Uniforms, ptr addrspace(1) %0, i64 0, i32 1
  %10 = bitcast ptr addrspace(1) %9 to ptr addrspace(1)
  store <4 x float> %8, ptr addrspace(1) %10, align 16

  ; dst.basis[1] = src.basis[1]. These descend into the column, so the pointer
  ; names the matrix's <3 x float> element type, and the bitcast re-types it to
  ; the 16-byte padded column the value is written as.
  %11 = getelementptr inbounds %struct.Uniforms, ptr addrspace(2) %1, i64 0, i32 1, i32 0, i64 1
  %12 = bitcast ptr addrspace(2) %11 to ptr addrspace(2)
  %13 = load <4 x float>, ptr addrspace(2) %12, align 16
  %14 = getelementptr inbounds %struct.Uniforms, ptr addrspace(1) %0, i64 0, i32 1, i32 0, i64 1
  %15 = bitcast ptr addrspace(1) %14 to ptr addrspace(1)
  store <4 x float> %13, ptr addrspace(1) %15, align 16

  ; dst.basis[2] = src.basis[2]. Same as above, index 2.
  %16 = getelementptr inbounds %struct.Uniforms, ptr addrspace(2) %1, i64 0, i32 1, i32 0, i64 2
  %17 = bitcast ptr addrspace(2) %16 to ptr addrspace(2)
  %18 = load <4 x float>, ptr addrspace(2) %17, align 16
  %19 = getelementptr inbounds %struct.Uniforms, ptr addrspace(1) %0, i64 0, i32 1, i32 0, i64 2
  %20 = bitcast ptr addrspace(1) %19 to ptr addrspace(1)
  store <4 x float> %18, ptr addrspace(1) %20, align 16

  ret void
}

attributes #0 = { norecurse nounwind "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "frame-pointer"="all" "less-precise-fpmad"="false" "no-infs-fp-math"="true" "no-jump-tables"="false" "no-nans-fp-math"="true" "no-signed-zeros-fp-math"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "unsafe-fp-math"="true" "use-soft-float"="false" }

!llvm.module.flags = !{!0, !1}
!llvm.ident = !{!2}
!air.version = !{!3}
!air.language_version = !{!4}
!air.compile_options = !{!5, !6, !7}
!air.kernel = !{!8}

!0 = !{i32 2, !"SDK Version", [3 x i32] [i32 10, i32 15, i32 6]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{!"Apple LLVM version 902.14 (metalfe-902.14.12)"}
!3 = !{i32 2, i32 2, i32 0}
!4 = !{!"Metal", i32 2, i32 2, i32 0}
!5 = !{!"air.compile.denorms_disable"}
!6 = !{!"air.compile.fast_math_enable"}
!7 = !{!"air.compile.framebuffer_fetch_disable"}
!8 = !{ptr @write_columns, !9, !10}
!9 = !{}
!10 = !{!11, !12}
!11 = !{i32 0, !"air.buffer", !"air.buffer_size", i32 64, !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.struct_type_info", !13, !"air.arg_type_size", i32 64, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"Uniforms", !"air.arg_name", !"dst"}
!12 = !{i32 1, !"air.buffer", !"air.buffer_size", i32 64, !"air.location_index", i32 1, i32 1, !"air.read", !"air.struct_type_info", !13, !"air.arg_type_size", i32 64, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"Uniforms", !"air.arg_name", !"src"}
!13 = !{i32 0, i32 16, i32 0, !"float4", !"color", i32 16, i32 48, i32 0, !"float3x3", !"basis"}

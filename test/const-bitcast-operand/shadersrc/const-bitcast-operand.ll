; ModuleID = 'constant_bitcast_operand'
source_filename = "constant_bitcast_operand"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx10.15.0"

; Hand-written AIR for shadersrc/const-bitcast-operand.metal. See that file, and
; the README next to it, for why this is checked in rather than compiled.
;
; The interesting instruction is %2. Its operand is a constant, so the
; translator emits the operand through declareConstantComposite and never
; records a result type for it; looking that type up therefore finds nothing.
; This is the only AIR shape in the corpus where a bitcast's source type cannot
; be looked up, and the only one that reaches the fallback in the LLVMBitCast
; case of src/iridium/air.cpp.

; Function Attrs: norecurse nounwind memory(readwrite)
define void @constant_bitcast_operand(ptr addrspace(1) noalias captures(none) %0, ptr addrspace(2) noalias readonly captures(none) dereferenceable(16) %1) local_unnamed_addr #0 {
entry:
  %2 = bitcast <2 x i32> <i32 7, i32 8> to <2 x float>

  ; dst[0] = src. One float4 each way, so the two getelementptrs agree and both
  ; stores are well-typed; the readback this fixture checks is that the module
  ; still runs once the bitcast above has been translated.
  %3 = getelementptr inbounds <4 x float>, ptr addrspace(2) %1, i64 0
  %4 = load <4 x float>, ptr addrspace(2) %3, align 16
  %5 = getelementptr inbounds <4 x float>, ptr addrspace(1) %0, i64 0
  store <4 x float> %4, ptr addrspace(1) %5, align 16

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
!8 = !{ptr @constant_bitcast_operand, !9, !10}
!9 = !{}
!10 = !{!11, !12}
!11 = !{i32 0, !"air.buffer", !"air.buffer_size", i32 16, !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4", !"air.arg_name", !"dst"}
!12 = !{i32 1, !"air.buffer", !"air.buffer_size", i32 16, !"air.location_index", i32 1, i32 1, !"air.read", !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4", !"air.arg_name", !"src"}

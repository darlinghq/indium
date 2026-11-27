; ModuleID = 'ri-O2.air'
source_filename = "air-reinterpret.metal"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64_v28-apple-macosx26.0.0"

%class.Counters = type { i32, float }
%struct.Quad = type { float, float, float, float }

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite)
define void @reinterpret_load(ptr addrspace(1) noundef writeonly captures(none) "air-buffer-no-alias" %0, ptr addrspace(2) noundef readonly captures(none) "air-buffer-no-alias" %1, ptr addrspace(1) noundef captures(none) "air-buffer-no-alias" %2, i32 noundef %3) local_unnamed_addr #0 {
  %5 = zext i32 %3 to i64
  %6 = getelementptr inbounds <4 x float>, ptr addrspace(2) %1, i64 %5
  %7 = load <4 x float>, ptr addrspace(2) %6, align 16, !alias.scope !29, !noalias !32
  %8 = extractelement <4 x float> %7, i64 3
  %9 = getelementptr inbounds %class.Counters, ptr addrspace(1) %2, i64 0, i32 1
  %10 = load float, ptr addrspace(1) %9, align 4, !tbaa !35, !alias.scope !41, !noalias !42
  %11 = fmul fast float %8, %10
  %q = alloca %struct.Quad, align 16
  %qx = extractelement <4 x float> %7, i64 0
  %qy = extractelement <4 x float> %7, i64 1
  %qz = extractelement <4 x float> %7, i64 2
  %qp0 = getelementptr inbounds %struct.Quad, ptr %q, i32 0, i32 0
  store float %qx, ptr %qp0, align 4
  %qp1 = getelementptr inbounds %struct.Quad, ptr %q, i32 0, i32 1
  store float %qy, ptr %qp1, align 4
  %qp2 = getelementptr inbounds %struct.Quad, ptr %q, i32 0, i32 2
  store float %qz, ptr %qp2, align 4
  %qp3 = getelementptr inbounds %struct.Quad, ptr %q, i32 0, i32 3
  store float %11, ptr %qp3, align 4
  %12 = load <4 x float>, ptr %q, align 16
  %13 = getelementptr inbounds <4 x float>, ptr addrspace(1) %0, i64 %5
  store <4 x float> %12, ptr addrspace(1) %13, align 16, !tbaa !43, !alias.scope !44, !noalias !45
  %14 = getelementptr inbounds %class.Counters, ptr addrspace(1) %2, i64 0, i32 0
  store i32 7, ptr addrspace(1) %14, align 4, !tbaa !46, !alias.scope !41, !noalias !42
  ret void
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: write)
define void @reinterpret_store(ptr addrspace(1) noundef writeonly captures(none) "air-buffer-no-alias" %0, ptr addrspace(2) noundef readonly captures(none) "air-buffer-no-alias" %1, i32 noundef %2) local_unnamed_addr #1 {
  %4 = zext i32 %2 to i64
  %5 = getelementptr inbounds <4 x float>, ptr addrspace(2) %1, i64 %4
  %6 = load <4 x float>, ptr addrspace(2) %5, align 16, !alias.scope !47, !noalias !50
  %q = alloca %struct.Quad, align 16
  store <4 x float> %6, ptr %q, align 16
  %qp0 = getelementptr inbounds %struct.Quad, ptr %q, i32 0, i32 0
  %7 = load float, ptr %qp0, align 4
  %qp1 = getelementptr inbounds %struct.Quad, ptr %q, i32 0, i32 1
  %8 = load float, ptr %qp1, align 4
  %qp2 = getelementptr inbounds %struct.Quad, ptr %q, i32 0, i32 2
  %9 = load float, ptr %qp2, align 4
  %qp3 = getelementptr inbounds %struct.Quad, ptr %q, i32 0, i32 3
  %10 = load float, ptr %qp3, align 4
  %11 = getelementptr inbounds %struct.Quad, ptr addrspace(1) %0, i64 %4, i32 0
  store float %7, ptr addrspace(1) %11, align 4, !tbaa !52, !alias.scope !50, !noalias !47
  %12 = getelementptr inbounds %struct.Quad, ptr addrspace(1) %0, i64 %4, i32 1
  store float %8, ptr addrspace(1) %12, align 4, !tbaa !54, !alias.scope !50, !noalias !47
  %13 = getelementptr inbounds %struct.Quad, ptr addrspace(1) %0, i64 %4, i32 2
  store float %9, ptr addrspace(1) %13, align 4, !tbaa !55, !alias.scope !50, !noalias !47
  %14 = getelementptr inbounds %struct.Quad, ptr addrspace(1) %0, i64 %4, i32 3
  store float %10, ptr addrspace(1) %14, align 4, !tbaa !56, !alias.scope !50, !noalias !47
  ret void
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) "approx-func-fp-math"="true" "frame-pointer"="all" "min-legal-vector-width"="0" "no-builtins" "no-infs-fp-math"="true" "no-nans-fp-math"="true" "no-signed-zeros-fp-math"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "unsafe-fp-math"="true" }
attributes #1 = { mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: write) "approx-func-fp-math"="true" "frame-pointer"="all" "min-legal-vector-width"="0" "no-builtins" "no-infs-fp-math"="true" "no-nans-fp-math"="true" "no-signed-zeros-fp-math"="true" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "unsafe-fp-math"="true" }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6, !7, !8}
!air.kernel = !{!9, !17}
!air.compile_options = !{!22, !23, !24}
!llvm.ident = !{!25}
!air.version = !{!26}
!air.language_version = !{!27}
!air.source_file_name = !{!28}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 26, i32 5]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 7, !"frame-pointer", i32 2}
!3 = !{i32 7, !"air.max_device_buffers", i32 31}
!4 = !{i32 7, !"air.max_constant_buffers", i32 31}
!5 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!6 = !{i32 7, !"air.max_textures", i32 128}
!7 = !{i32 7, !"air.max_read_write_textures", i32 8}
!8 = !{i32 7, !"air.max_samplers", i32 16}
!9 = !{ptr @reinterpret_load, !10, !11}
!10 = !{}
!11 = !{!12, !13, !14, !16}
!12 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4", !"air.arg_name", !"out"}
!13 = !{i32 1, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.address_space", i32 2, !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4", !"air.arg_name", !"in"}
!14 = !{i32 2, !"air.buffer", !"air.location_index", i32 2, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.struct_type_info", !15, !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"Counters", !"air.arg_name", !"counters"}
!15 = !{i32 0, i32 4, i32 0, !"uint", !"count", i32 4, i32 4, i32 0, !"float", !"scale"}
!16 = !{i32 3, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"tid"}
!17 = !{ptr @reinterpret_store, !10, !18}
!18 = !{!19, !13, !21}
!19 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read_write", !"air.address_space", i32 1, !"air.struct_type_info", !20, !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 4, !"air.arg_type_name", !"Quad", !"air.arg_name", !"out"}
!20 = !{i32 0, i32 4, i32 0, !"float", !"a", i32 4, i32 4, i32 0, !"float", !"b", i32 8, i32 4, i32 0, !"float", !"c", i32 12, i32 4, i32 0, !"float", !"d"}
!21 = !{i32 2, !"air.thread_position_in_grid", !"air.arg_type_name", !"uint", !"air.arg_name", !"tid"}
!22 = !{!"air.compile.denorms_disable"}
!23 = !{!"air.compile.fast_math_enable"}
!24 = !{!"air.compile.framebuffer_fetch_enable"}
!25 = !{!"Apple metal version 32023.883 (metalfe-32023.883)"}
!26 = !{i32 2, i32 8, i32 0}
!27 = !{!"Metal", i32 4, i32 0, i32 0}
!28 = !{!"/private/tmp/vdmc.3mBt/air-reinterpret.metal"}
!29 = !{!30}
!30 = distinct !{!30, !31, !"air-alias-scope-arg(1)"}
!31 = distinct !{!31, !"air-alias-scopes(reinterpret_load)"}
!32 = !{!33, !34}
!33 = distinct !{!33, !31, !"air-alias-scope-arg(0)"}
!34 = distinct !{!34, !31, !"air-alias-scope-arg(2)"}
!35 = !{!36, !40, i64 4}
!36 = !{!"_ZTS8Counters", !37, i64 0, !40, i64 4}
!37 = !{!"int", !38, i64 0}
!38 = !{!"omnipotent char", !39, i64 0}
!39 = !{!"Simple C++ TBAA"}
!40 = !{!"float", !38, i64 0}
!41 = !{!34}
!42 = !{!33, !30}
!43 = !{!38, !38, i64 0}
!44 = !{!33}
!45 = !{!30, !34}
!46 = !{!36, !37, i64 0}
!47 = !{!48}
!48 = distinct !{!48, !49, !"air-alias-scope-arg(1)"}
!49 = distinct !{!49, !"air-alias-scopes(reinterpret_store)"}
!50 = !{!51}
!51 = distinct !{!51, !49, !"air-alias-scope-arg(0)"}
!52 = !{!53, !40, i64 0}
!53 = !{!"_ZTS4Quad", !40, i64 0, !40, i64 4, !40, i64 8, !40, i64 12}
!54 = !{!53, !40, i64 4}
!55 = !{!53, !40, i64 8}
!56 = !{!53, !40, i64 12}

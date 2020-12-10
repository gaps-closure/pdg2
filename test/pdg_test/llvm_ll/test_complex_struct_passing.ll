; ModuleID = 'test_complex_struct_passing.c'
source_filename = "test_complex_struct_passing.c"
target datalayout = "e-m:o-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.15.0"

%struct.clothes = type { [10 x i8], i32 }
%struct.person_t = type { i32, [10 x i8], %struct.clothes }

@.str = private unnamed_addr constant [18 x i8] c"clothes color %c.\00", align 1
@main.c = private unnamed_addr constant %struct.clothes { [10 x i8] c"red\00\00\00\00\00\00\00", i32 5 }, align 4
@.str.1 = private unnamed_addr constant [10 x i8] c"Jack\00\00\00\00\00\00", align 1

; Function Attrs: noinline nounwind optnone ssp uwtable
define void @f(%struct.person_t*) #0 !dbg !8 {
  %2 = alloca %struct.person_t*, align 8
  store %struct.person_t* %0, %struct.person_t** %2, align 8
  call void @llvm.dbg.declare(metadata %struct.person_t** %2, metadata !28, metadata !29), !dbg !30
  %3 = load %struct.person_t*, %struct.person_t** %2, align 8, !dbg !31
  %4 = getelementptr inbounds %struct.person_t, %struct.person_t* %3, i32 0, i32 1, !dbg !32
  %5 = getelementptr inbounds [10 x i8], [10 x i8]* %4, i64 0, i64 9, !dbg !31
  %6 = load i8, i8* %5, align 1, !dbg !31
  %7 = sext i8 %6 to i32, !dbg !31
  %8 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([18 x i8], [18 x i8]* @.str, i32 0, i32 0), i32 %7), !dbg !33
  ret void, !dbg !34
}

; Function Attrs: nounwind readnone speculatable
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

declare i32 @printf(i8*, ...) #2

; Function Attrs: noinline nounwind optnone ssp uwtable
define i32 @main() #0 !dbg !35 {
  %1 = alloca i32, align 4
  %2 = alloca %struct.clothes, align 4
  %3 = alloca %struct.person_t, align 4
  %4 = alloca %struct.person_t*, align 8
  store i32 0, i32* %1, align 4
  call void @llvm.dbg.declare(metadata %struct.clothes* %2, metadata !38, metadata !29), !dbg !39
  %5 = bitcast %struct.clothes* %2 to i8*, !dbg !39
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %5, i8* getelementptr inbounds (%struct.clothes, %struct.clothes* @main.c, i32 0, i32 0, i32 0), i64 16, i32 4, i1 false), !dbg !39
  call void @llvm.dbg.declare(metadata %struct.person_t* %3, metadata !40, metadata !29), !dbg !41
  %6 = getelementptr inbounds %struct.person_t, %struct.person_t* %3, i32 0, i32 0, !dbg !42
  store i32 10, i32* %6, align 4, !dbg !42
  %7 = getelementptr inbounds %struct.person_t, %struct.person_t* %3, i32 0, i32 1, !dbg !42
  %8 = bitcast [10 x i8]* %7 to i8*, !dbg !43
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %8, i8* getelementptr inbounds ([10 x i8], [10 x i8]* @.str.1, i32 0, i32 0), i64 10, i32 1, i1 false), !dbg !43
  %9 = getelementptr inbounds %struct.person_t, %struct.person_t* %3, i32 0, i32 2, !dbg !42
  %10 = bitcast %struct.clothes* %9 to i8*, !dbg !44
  %11 = bitcast %struct.clothes* %2 to i8*, !dbg !44
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %10, i8* %11, i64 16, i32 4, i1 false), !dbg !44
  call void @llvm.dbg.declare(metadata %struct.person_t** %4, metadata !45, metadata !29), !dbg !46
  store %struct.person_t* %3, %struct.person_t** %4, align 8, !dbg !46
  %12 = load %struct.person_t*, %struct.person_t** %4, align 8, !dbg !47
  %13 = getelementptr inbounds %struct.person_t, %struct.person_t* %12, i32 0, i32 2, !dbg !48
  %14 = bitcast %struct.clothes* %13 to %struct.person_t*, !dbg !49
  call void @f(%struct.person_t* %14), !dbg !50
  ret i32 0, !dbg !51
}

; Function Attrs: argmemonly nounwind
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* nocapture writeonly, i8* nocapture readonly, i64, i32, i1) #3

attributes #0 = { noinline nounwind optnone ssp uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+cx16,+fxsr,+mmx,+sse,+sse2,+sse3,+sse4.1,+ssse3,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone speculatable }
attributes #2 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+cx16,+fxsr,+mmx,+sse,+sse2,+sse3,+sse4.1,+ssse3,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { argmemonly nounwind }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5, !6}
!llvm.ident = !{!7}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 5.0.2 (tags/RELEASE_502/final)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2)
!1 = !DIFile(filename: "test_complex_struct_passing.c", directory: "/Users/yongzhehuang/Documents/llvm_versions/program-dependence-graph-in-llvm/test/pdg_test")
!2 = !{}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{i32 7, !"PIC Level", i32 2}
!7 = !{!"clang version 5.0.2 (tags/RELEASE_502/final)"}
!8 = distinct !DISubprogram(name: "f", scope: !1, file: !1, line: 14, type: !9, isLocal: false, isDefinition: true, scopeLine: 14, flags: DIFlagPrototyped, isOptimized: false, unit: !0, variables: !2)
!9 = !DISubroutineType(types: !10)
!10 = !{null, !11}
!11 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !12, size: 64)
!12 = !DIDerivedType(tag: DW_TAG_typedef, name: "Person", file: !1, line: 12, baseType: !13)
!13 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "person_t", file: !1, line: 8, size: 256, elements: !14)
!14 = !{!15, !17, !22}
!15 = !DIDerivedType(tag: DW_TAG_member, name: "age", scope: !13, file: !1, line: 9, baseType: !16, size: 32)
!16 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!17 = !DIDerivedType(tag: DW_TAG_member, name: "name", scope: !13, file: !1, line: 10, baseType: !18, size: 80, offset: 32)
!18 = !DICompositeType(tag: DW_TAG_array_type, baseType: !19, size: 80, elements: !20)
!19 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!20 = !{!21}
!21 = !DISubrange(count: 10)
!22 = !DIDerivedType(tag: DW_TAG_member, name: "s", scope: !13, file: !1, line: 11, baseType: !23, size: 128, offset: 128)
!23 = !DIDerivedType(tag: DW_TAG_typedef, name: "Clothes", file: !1, line: 6, baseType: !24)
!24 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "clothes", file: !1, line: 3, size: 128, elements: !25)
!25 = !{!26, !27}
!26 = !DIDerivedType(tag: DW_TAG_member, name: "color", scope: !24, file: !1, line: 4, baseType: !18, size: 80)
!27 = !DIDerivedType(tag: DW_TAG_member, name: "length", scope: !24, file: !1, line: 5, baseType: !16, size: 32, offset: 96)
!28 = !DILocalVariable(name: "p1", arg: 1, scope: !8, file: !1, line: 14, type: !11)
!29 = !DIExpression()
!30 = !DILocation(line: 14, column: 16, scope: !8)
!31 = !DILocation(line: 15, column: 33, scope: !8)
!32 = !DILocation(line: 15, column: 37, scope: !8)
!33 = !DILocation(line: 15, column: 5, scope: !8)
!34 = !DILocation(line: 16, column: 1, scope: !8)
!35 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 18, type: !36, isLocal: false, isDefinition: true, scopeLine: 18, isOptimized: false, unit: !0, variables: !2)
!36 = !DISubroutineType(types: !37)
!37 = !{!16}
!38 = !DILocalVariable(name: "c", scope: !35, file: !1, line: 19, type: !23)
!39 = !DILocation(line: 19, column: 13, scope: !35)
!40 = !DILocalVariable(name: "p", scope: !35, file: !1, line: 20, type: !12)
!41 = !DILocation(line: 20, column: 12, scope: !35)
!42 = !DILocation(line: 20, column: 16, scope: !35)
!43 = !DILocation(line: 20, column: 21, scope: !35)
!44 = !DILocation(line: 20, column: 29, scope: !35)
!45 = !DILocalVariable(name: "pt", scope: !35, file: !1, line: 21, type: !11)
!46 = !DILocation(line: 21, column: 13, scope: !35)
!47 = !DILocation(line: 22, column: 8, scope: !35)
!48 = !DILocation(line: 22, column: 12, scope: !35)
!49 = !DILocation(line: 22, column: 7, scope: !35)
!50 = !DILocation(line: 22, column: 5, scope: !35)
!51 = !DILocation(line: 23, column: 5, scope: !35)

; ModuleID = 'test_arr.c'
source_filename = "test_arr.c"
target datalayout = "e-m:o-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx10.15.0"

%struct.st = type { [10 x i32], i32* }

@.str = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

; Function Attrs: noinline nounwind optnone ssp uwtable
define void @test1(%struct.st*) #0 !dbg !8 {
  %2 = alloca %struct.st*, align 8
  store %struct.st* %0, %struct.st** %2, align 8
  call void @llvm.dbg.declare(metadata %struct.st** %2, metadata !21, metadata !22), !dbg !23
  %3 = load %struct.st*, %struct.st** %2, align 8, !dbg !24
  %4 = getelementptr inbounds %struct.st, %struct.st* %3, i32 0, i32 0, !dbg !25
  %5 = getelementptr inbounds [10 x i32], [10 x i32]* %4, i64 0, i64 0, !dbg !24
  %6 = load i32, i32* %5, align 8, !dbg !24
  %7 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str, i32 0, i32 0), i32 %6), !dbg !26
  ret void, !dbg !27
}

; Function Attrs: nounwind readnone speculatable
declare void @llvm.dbg.declare(metadata, metadata, metadata) #1

declare i32 @printf(i8*, ...) #2

; Function Attrs: noinline nounwind optnone ssp uwtable
define void @test2(i32*, i32) #0 !dbg !28 {
  %3 = alloca i32*, align 8
  %4 = alloca i32, align 4
  %5 = alloca i32*, align 8
  store i32* %0, i32** %3, align 8
  call void @llvm.dbg.declare(metadata i32** %3, metadata !31, metadata !22), !dbg !32
  store i32 %1, i32* %4, align 4
  call void @llvm.dbg.declare(metadata i32* %4, metadata !33, metadata !22), !dbg !34
  call void @llvm.dbg.declare(metadata i32** %5, metadata !35, metadata !22), !dbg !36
  %6 = load i32*, i32** %3, align 8, !dbg !37
  %7 = getelementptr inbounds i32, i32* %6, i64 2, !dbg !37
  store i32* %7, i32** %5, align 8, !dbg !36
  %8 = load i32*, i32** %5, align 8, !dbg !38
  %9 = load i32, i32* %8, align 4, !dbg !39
  %10 = call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([4 x i8], [4 x i8]* @.str, i32 0, i32 0), i32 %9), !dbg !40
  ret void, !dbg !41
}

attributes #0 = { noinline nounwind optnone ssp uwtable "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+cx16,+fxsr,+mmx,+sse,+sse2,+sse3,+sse4.1,+ssse3,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind readnone speculatable }
attributes #2 = { "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+cx16,+fxsr,+mmx,+sse,+sse2,+sse3,+sse4.1,+ssse3,+x87" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3, !4, !5, !6}
!llvm.ident = !{!7}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, producer: "clang version 5.0.2 (tags/RELEASE_502/final)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, enums: !2)
!1 = !DIFile(filename: "test_arr.c", directory: "/Users/yongzhehuang/Documents/llvm_versions/program-dependence-graph-in-llvm/test/pdg_test")
!2 = !{}
!3 = !{i32 2, !"Dwarf Version", i32 4}
!4 = !{i32 2, !"Debug Info Version", i32 3}
!5 = !{i32 1, !"wchar_size", i32 4}
!6 = !{i32 7, !"PIC Level", i32 2}
!7 = !{!"clang version 5.0.2 (tags/RELEASE_502/final)"}
!8 = distinct !DISubprogram(name: "test1", scope: !1, file: !1, line: 7, type: !9, isLocal: false, isDefinition: true, scopeLine: 7, flags: DIFlagPrototyped, isOptimized: false, unit: !0, variables: !2)
!9 = !DISubroutineType(types: !10)
!10 = !{null, !11}
!11 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !12, size: 64)
!12 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "st", file: !1, line: 2, size: 384, elements: !13)
!13 = !{!14, !19}
!14 = !DIDerivedType(tag: DW_TAG_member, name: "f1", scope: !12, file: !1, line: 3, baseType: !15, size: 320)
!15 = !DICompositeType(tag: DW_TAG_array_type, baseType: !16, size: 320, elements: !17)
!16 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!17 = !{!18}
!18 = !DISubrange(count: 10)
!19 = !DIDerivedType(tag: DW_TAG_member, name: "f2", scope: !12, file: !1, line: 4, baseType: !20, size: 64, offset: 320)
!20 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !16, size: 64)
!21 = !DILocalVariable(name: "ss", arg: 1, scope: !8, file: !1, line: 7, type: !11)
!22 = !DIExpression()
!23 = !DILocation(line: 7, column: 23, scope: !8)
!24 = !DILocation(line: 8, column: 19, scope: !8)
!25 = !DILocation(line: 8, column: 23, scope: !8)
!26 = !DILocation(line: 8, column: 4, scope: !8)
!27 = !DILocation(line: 9, column: 1, scope: !8)
!28 = distinct !DISubprogram(name: "test2", scope: !1, file: !1, line: 12, type: !29, isLocal: false, isDefinition: true, scopeLine: 12, flags: DIFlagPrototyped, isOptimized: false, unit: !0, variables: !2)
!29 = !DISubroutineType(types: !30)
!30 = !{null, !20, !16}
!31 = !DILocalVariable(name: "s", arg: 1, scope: !28, file: !1, line: 12, type: !20)
!32 = !DILocation(line: 12, column: 17, scope: !28)
!33 = !DILocalVariable(name: "len", arg: 2, scope: !28, file: !1, line: 12, type: !16)
!34 = !DILocation(line: 12, column: 24, scope: !28)
!35 = !DILocalVariable(name: "c", scope: !28, file: !1, line: 13, type: !20)
!36 = !DILocation(line: 13, column: 10, scope: !28)
!37 = !DILocation(line: 13, column: 15, scope: !28)
!38 = !DILocation(line: 14, column: 21, scope: !28)
!39 = !DILocation(line: 14, column: 20, scope: !28)
!40 = !DILocation(line: 14, column: 5, scope: !28)
!41 = !DILocation(line: 17, column: 1, scope: !28)

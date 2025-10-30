; ModuleID = 'test2.ll'
source_filename = "build/tests/test2.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@g = dso_local global i32 10, align 4

; Function Attrs: noinline nounwind uwtable
define dso_local i32 @foo(i32 noundef %n) #0 {
entry:
  %n.addr = alloca i32, align 4
  %sum = alloca i32, align 4
  %i = alloca i32, align 4
  store i32 %n, ptr %n.addr, align 4
  store i32 0, ptr %sum, align 4
  store i32 0, ptr %i, align 4
  br label %if_guard

if_guard:                                         ; preds = %entry
  %0 = load i32, ptr %i, align 4
  %1 = load i32, ptr %n.addr, align 4
  %2 = icmp slt i32 %0, %1
  br i1 %2, label %hoist_point, label %for.end

hoist_point:                                      ; preds = %if_guard
  %3 = load i32, ptr %n.addr, align 4
  %4 = load i32, ptr @g, align 4
  br label %for.cond

for.cond:                                         ; preds = %hoist_point, %for.inc
  %5 = load i32, ptr %i, align 4
  %cmp = icmp slt i32 %5, %3
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %6 = load i32, ptr %sum, align 4
  %add = add nsw i32 %6, %4
  store i32 %add, ptr %sum, align 4
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %7 = load i32, ptr %i, align 4
  %inc = add nsw i32 %7, 1
  store i32 %inc, ptr %i, align 4
  br label %for.cond, !llvm.loop !6

for.end:                                          ; preds = %if_guard, %for.cond
  %8 = load i32, ptr %sum, align 4
  ret i32 %8
}

attributes #0 = { noinline nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"clang version 22.0.0git (https://github.com/llvm/llvm-project.git 5d0f1591f8b91ac7919910c4e3e9614a8804c02a)"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}

; ModuleID = 'input2-opt.bc'
source_filename = "input2.ll"

define i32 @ub() {
entry:
  %add = add nsw i32 32, 42
  %div = sdiv i32 %add, 0
  ret i32 %div
}

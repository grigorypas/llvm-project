; RUN: opt < %s -S -passes=always-inline | FileCheck %s

; Test 1: Regular function (not marked with flatten_deep) calls another function.
; The call should NOT be inlined since there's no alwaysinline attribute.

define i32 @callee_simple() {
; CHECK-LABEL: define i32 @callee_simple(
; CHECK-NEXT:    ret i32 42
;
  ret i32 42
}

define i32 @caller_simple() {
; CHECK-LABEL: define i32 @caller_simple(
; CHECK-NEXT:    [[CALL:%.*]] = call i32 @callee_simple()
; CHECK-NEXT:    ret i32 [[CALL]]
;
  %call = call i32 @callee_simple()
  ret i32 %call
}

; Test 2: Function A calls B and C, function C calls D.
; With flatten_deep attribute, all functions should be inlined.

define i32 @funcD() {
; CHECK-LABEL: define i32 @funcD(
; CHECK-NEXT:    ret i32 4
;
  ret i32 4
}

define i32 @funcC() {
; CHECK-LABEL: define i32 @funcC(
; CHECK-NEXT:    [[CALL:%.*]] = call i32 @funcD()
; CHECK-NEXT:    [[ADD:%.*]] = add i32 [[CALL]], 3
; CHECK-NEXT:    ret i32 [[ADD]]
;
  %call = call i32 @funcD()
  %add = add i32 %call, 3
  ret i32 %add
}

define i32 @funcB() {
; CHECK-LABEL: define i32 @funcB(
; CHECK-NEXT:    ret i32 2
;
  ret i32 2
}

define i32 @funcA() #0 {
; CHECK-LABEL: define i32 @funcA(
; CHECK-NEXT:    [[ADD_I:%.*]] = add i32 4, 3
; CHECK-NEXT:    [[ADD:%.*]] = add i32 2, [[ADD_I]]
; CHECK-NEXT:    ret i32 [[ADD]]
;
  %call1 = call i32 @funcB()
  %call2 = call i32 @funcC()
  %add = add i32 %call1, %call2
  ret i32 %add
}

; Test 3: Function A calls B and C. C is external (declaration only).
; B should be inlined, but C should not be inlined (external function).

declare i32 @funcC_external()

define i32 @funcB_inline() {
; CHECK-LABEL: define i32 @funcB_inline(
; CHECK-NEXT:    ret i32 10
;
  ret i32 10
}

define i32 @funcA_external() #0 {
; CHECK-LABEL: define i32 @funcA_external(
; CHECK-NEXT:    [[CALL2:%.*]] = call i32 @funcC_external()
; CHECK-NEXT:    [[ADD:%.*]] = add i32 10, [[CALL2]]
; CHECK-NEXT:    ret i32 [[ADD]]
;
  %call1 = call i32 @funcB_inline()
  %call2 = call i32 @funcC_external()
  %add = add i32 %call1, %call2
  ret i32 %add
}

; Test 4: Test depth limit. A->B->C->D with depth=2.
; B and C should be inlined, but not D (exceeds depth limit).

define i32 @funcD_depth() {
; CHECK-LABEL: define i32 @funcD_depth(
; CHECK-NEXT:    ret i32 100
;
  ret i32 100
}

define i32 @funcC_depth() {
; CHECK-LABEL: define i32 @funcC_depth(
; CHECK-NEXT:    [[CALL:%.*]] = call i32 @funcD_depth()
; CHECK-NEXT:    [[MUL:%.*]] = mul i32 [[CALL]], 2
; CHECK-NEXT:    ret i32 [[MUL]]
;
  %call = call i32 @funcD_depth()
  %mul = mul i32 %call, 2
  ret i32 %mul
}

define i32 @funcB_depth() {
; CHECK-LABEL: define i32 @funcB_depth(
; CHECK-NEXT:    [[CALL:%.*]] = call i32 @funcC_depth()
; CHECK-NEXT:    [[ADD:%.*]] = add i32 [[CALL]], 5
; CHECK-NEXT:    ret i32 [[ADD]]
;
  %call = call i32 @funcC_depth()
  %add = add i32 %call, 5
  ret i32 %add
}

define i32 @funcA_depth() #1 {
; CHECK-LABEL: define i32 @funcA_depth(
; CHECK-NEXT:    [[CALL_I1:%.*]] = call i32 @funcD_depth()
; CHECK-NEXT:    [[MUL_I:%.*]] = mul i32 [[CALL_I1]], 2
; CHECK-NEXT:    [[ADD_I:%.*]] = add i32 [[MUL_I]], 5
; CHECK-NEXT:    ret i32 [[ADD_I]]
;
  %call = call i32 @funcB_depth()
  ret i32 %call
}

attributes #0 = { flatten_deep=10 }
attributes #1 = { flatten_deep=2 }

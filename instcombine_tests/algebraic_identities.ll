; Test file for algebraic identities optimization
; These should be simplified by the InstCombine pass

define i32 @test_add_zero(i32 %x) {
entry:
  ; X + 0 should become X
  %result1 = add i32 %x, 0
  
  ; 0 + X should become X
  %result2 = add i32 0, %x
  
  ; Combine both results (this will be X + X, which becomes shl X, 1)
  %final = add i32 %result1, %result2
  ret i32 %final
}

define i32 @test_mul_identity(i32 %x) {
entry:
  ; X * 1 should become X
  %result1 = mul i32 %x, 1
  
  ; 1 * X should become X
  %result2 = mul i32 1, %x
  
  ; X * 0 should become 0
  %zero = mul i32 %x, 0
  
  ; Add them (X + X + 0 = X + X = shl X, 1)
  %temp = add i32 %result1, %result2
  %final = add i32 %temp, %zero
  ret i32 %final
}

define i32 @test_sub_zero(i32 %x) {
entry:
  ; X - 0 should become X
  %result = sub i32 %x, 0
  ret i32 %result
}

define i32 @test_self_operations(i32 %x) {
entry:
  ; X - X should become 0
  %zero1 = sub i32 %x, %x
  
  ; X ^ X should become 0
  %zero2 = xor i32 %x, %x
  
  ; 0 + 0 should become 0
  %result = add i32 %zero1, %zero2
  ret i32 %result
}
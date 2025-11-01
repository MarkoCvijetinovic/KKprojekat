; Test file for canonicalization optimizations
; Constants should move to the right-hand side

define i32 @test_const_to_rhs(i32 %x, i32 %y) {
entry:
  ; 5 + X should become X + 5
  %add1 = add i32 5, %x
  
  ; 3 * X should become X * 3
  %mul1 = mul i32 3, %x
  
  ; 7 & X should become X & 7
  %and1 = and i32 7, %x
  
  ; 9 | X should become X | 9
  %or1 = or i32 9, %x
  
  ; 11 ^ X should become X ^ 11
  %xor1 = xor i32 11, %x
  
  ; Combine results
  %temp1 = add i32 %add1, %mul1
  %temp2 = add i32 %and1, %or1
  %temp3 = add i32 %temp1, %temp2
  %result = add i32 %temp3, %xor1
  ret i32 %result
}

define i32 @test_bitwise_logic(i32 %x) {
entry:
  ; X & X should become X
  %and_self = and i32 %x, %x
  
  ; X | X should become X
  %or_self = or i32 %x, %x
  
  ; X & 0 should become 0
  %and_zero = and i32 %x, 0
  
  ; X | -1 should become -1
  %or_ones = or i32 %x, -1
  
  ; X & -1 should become X
  %and_ones = and i32 %x, -1
  
  ; X | 0 should become X
  %or_zero = or i32 %x, 0
  
  ; Combine some results (many will be optimized away)
  %temp1 = add i32 %and_self, %or_self  ; X + X = shl X, 1
  %temp2 = add i32 %and_zero, %and_ones ; 0 + X = X
  %result = add i32 %temp1, %temp2      ; shl X, 1 + X
  ret i32 %result
}
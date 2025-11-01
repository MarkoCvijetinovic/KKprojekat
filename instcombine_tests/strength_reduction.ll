; Test file for strength reduction optimizations
; Multiplications and divisions by powers of 2 should become shifts

define i32 @test_mul_power_of_2(i32 %x) {
entry:
  ; X * 2 should become X << 1
  %mul2 = mul i32 %x, 2
  
  ; X * 4 should become X << 2
  %mul4 = mul i32 %x, 4
  
  ; X * 8 should become X << 3
  %mul8 = mul i32 %x, 8
  
  ; 16 * X should become X << 4 (after canonicalization)
  %mul16 = mul i32 16, %x
  
  ; Add them all together
  %temp1 = add i32 %mul2, %mul4
  %temp2 = add i32 %mul8, %mul16
  %result = add i32 %temp1, %temp2
  ret i32 %result
}

define i32 @test_udiv_power_of_2(i32 %x) {
entry:
  ; X / 2 should become X >> 1 (unsigned)
  %div2 = udiv i32 %x, 2
  
  ; X / 4 should become X >> 2 (unsigned)
  %div4 = udiv i32 %x, 4
  
  ; X / 8 should become X >> 3 (unsigned)
  %div8 = udiv i32 %x, 8
  
  ; Add results
  %temp = add i32 %div2, %div4
  %result = add i32 %temp, %div8
  ret i32 %result
}

define i32 @test_sdiv_power_of_2(i32 %x) {
entry:
  ; X / 2 should become X >> 1 (signed, arithmetic shift)
  %div2 = sdiv i32 %x, 2
  
  ; X / 4 should become X >> 2 (signed, arithmetic shift)
  %div4 = sdiv i32 %x, 4
  
  ; Add results
  %result = add i32 %div2, %div4
  ret i32 %result
}

define i32 @test_add_self(i32 %x) {
entry:
  ; X + X should become X << 1
  %result = add i32 %x, %x
  ret i32 %result
}
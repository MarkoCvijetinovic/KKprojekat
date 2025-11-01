; Test file for comparison optimizations
; Some relational comparisons can be converted to equality/inequality

define i1 @test_unsigned_comparisons(i32 %x) {
entry:
  ; x < 1 should become x == 0 (for unsigned)
  %cmp1 = icmp ult i32 %x, 1
  
  ; x >= 1 should become x != 0 (for unsigned)
  %cmp2 = icmp uge i32 %x, 1
  
  ; x <= 0 should become x == 0 (for unsigned)
  %cmp3 = icmp ule i32 %x, 0
  
  ; x > 0 should become x != 0 (for unsigned)
  %cmp4 = icmp ugt i32 %x, 0
  
  ; Combine results with logical operations
  %temp1 = and i1 %cmp1, %cmp2
  %temp2 = or i1 %cmp3, %cmp4
  %result = xor i1 %temp1, %temp2
  ret i1 %result
}

define i1 @test_boolean_comparisons(i1 %a, i1 %b) {
entry:
  ; a == b should become !(a ^ b) for boolean values
  %eq = icmp eq i1 %a, %b
  
  ; a != b should become a ^ b for boolean values
  %ne = icmp ne i1 %a, %b
  
  ; Combine the results
  %result = and i1 %eq, %ne  ; This should be optimized significantly
  ret i1 %result
}

define i32 @test_comparison_context(i32 %x, i32 %y) {
entry:
  ; Various comparisons that might be optimized
  %cmp1 = icmp eq i32 %x, 0
  %cmp2 = icmp ne i32 %y, 0
  
  ; Convert boolean results to integers and combine
  %ext1 = zext i1 %cmp1 to i32
  %ext2 = zext i1 %cmp2 to i32
  %result = add i32 %ext1, %ext2
  ret i32 %result
}
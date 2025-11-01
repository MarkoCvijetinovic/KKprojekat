; ModuleID = './tests/algebraic_identities.ll'
source_filename = "./tests/algebraic_identities.ll"

define i32 @test_add_zero(i32 %x) {
entry:
  %twox = shl i32 %x, 1
  ret i32 %twox
}

define i32 @test_mul_identity(i32 %x) {
entry:
  %twox = shl i32 %x, 1
  ret i32 %twox
}

define i32 @test_sub_zero(i32 %x) {
entry:
  ret i32 %x
}

define i32 @test_self_operations(i32 %x) {
entry:
  ret i32 0
}

// test9_variant_load.c
int foo(int *A, int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += A[i]; // depends on i, not invariant
    }
    return sum;
}


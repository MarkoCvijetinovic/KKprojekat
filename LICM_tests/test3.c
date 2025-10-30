// test3_pointer_store.c
int foo(int *A, int n) {
    int k = 42;
    for (int i = 0; i < n; i++) {
        A[i] = A[i] + k; // store, but pointer A invariant
    }
    return A[0];
}


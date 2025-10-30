// test6_nested_loops.c
int foo(int n, int m) {
    int a = 3;
    int b = 4;
    int sum = 0;
    for (int i = 0; i < n; i++) {
        int c = a + b; // invariant wrt inner loop
        for (int j = 0; j < m; j++) {
            sum += c;
        }
    }
    return sum;
}


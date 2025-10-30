// test2_invariant_load.c
int g = 10;

int foo(int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += g;  // load from global invariant
    }
    return sum;
}


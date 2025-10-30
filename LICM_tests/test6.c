// test10_conditional.c
int foo(int n) {
    int x = 3;
    int y = 5;
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            x += y * 2; // y*2 invariant
        }
    }
    return x;
}


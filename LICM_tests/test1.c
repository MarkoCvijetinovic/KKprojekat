// test1_simple_invariant.c
#include <stdlib.h>
#include <stdio.h>
int main(int n) {
    int x = 5;
    int sum = 0;
    for (int i = 0; i < n; i++) {
        sum += x * 2; // x*2 is loop-invariant
    }
    printf("%d\n",sum);
    return sum;
}


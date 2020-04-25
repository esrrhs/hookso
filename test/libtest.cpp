#include <stdio.h>

extern "C" bool libtest(int n) {
    printf("libtest %d \n", n);
    return false;
}

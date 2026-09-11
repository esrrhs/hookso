#include <stdio.h>
#include <stdint.h>

typedef int (*PutsFunc)(const char *s);

PutsFunc f = &puts;

extern "C" bool libtest(int n) {
    char buff[128] = {0};
    snprintf(buff, sizeof(buff), "libtest %d", n);
    if (n % 3 == 0) {
        puts(buff);
    } else if (n % 3 == 1) {
        f(buff);
    } else {
        PutsFunc ff = &puts;
        ff(buff);
    }
    fflush(stdout);
    return false;
}

extern "C" int libtest_args(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

extern "C" uint64_t libtest_u64() {
    return 0x100000001ULL;
}

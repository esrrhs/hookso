#include <stdio.h>

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
    return false;
}

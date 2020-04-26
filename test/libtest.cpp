#include <stdio.h>

extern "C" bool libtest(int n) {
    char buff[128] = {0};
    snprintf(buff, sizeof(buff), "libtest %d", n);
    puts(buff);
    return false;
}

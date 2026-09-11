#include <stdio.h>
#include <unistd.h>

extern "C" bool libtestnew(int n) {
    char buff[128] = {0};
    snprintf(buff, sizeof(buff), "libtestnew %d", n);
    puts(buff);
    fflush(stdout);
    return false;
}

extern "C" bool putsnew(const char *str) {
    char buff[128] = {0};
    snprintf(buff, sizeof(buff), "putsnew %s", str);
    puts(buff);
    fflush(stdout);
    return false;
}

extern "C" void mysleepnew() {
    puts("mysleepnew");
    fflush(stdout);
    usleep(50000);
}

extern "C" int libtestnew_sum(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

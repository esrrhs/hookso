#include <stdio.h>
#include <unistd.h>

extern "C" bool libtestnew(int n) {
    char buff[128] = {0};
    snprintf(buff, sizeof(buff), "libtestnew %d", n);
    puts(buff);
    return false;
}

extern "C" bool putsnew(const char *str) {
    char buff[128] = {0};
    snprintf(buff, sizeof(buff), "putsnew %s", str);
    puts(buff);
    return false;
}

extern "C" void mysleepnew() {
    puts("mysleepnew");
    sleep(1);
}

#include <stdio.h>

extern "C" bool putsnew(const char * str) {
    char buff[128] = {0};
    snprintf(buff, sizeof(buff), "putsnew %s", str);
    puts(buff);
    return false;
}

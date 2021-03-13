#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>

#define LIBTEST_PATH "libtest.so"

extern "C" bool libtest(int n);  //from libtest.so

extern "C" void mysleep() {
    sleep(1);
}

int main() {
    void *handle = dlopen(LIBTEST_PATH, RTLD_LAZY);

    if (NULL == handle)
        fprintf(stderr, "Failed to open \"%s\"!\n", LIBTEST_PATH);

    int n = 0;
    while (1) {
        if (libtest(n++)) {
            break;
        }
        mysleep();
    }

    dlclose(handle);

    return 0;
}

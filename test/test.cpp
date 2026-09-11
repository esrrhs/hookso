#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>

#define LIBTEST_PATH "libtest.so"

extern "C" bool libtest(int n);  //from libtest.so
extern "C" int libtest_args(int a, int b, int c, int d, int e, int f);

extern "C" void mysleep() {
    usleep(50000);
}

int main() {
    void *handle = dlopen(LIBTEST_PATH, RTLD_LAZY);

    if (NULL == handle)
        fprintf(stderr, "Failed to open \"%s\"!\n", LIBTEST_PATH);

    int n = 0;
    printf("MYSLEEP_ADDR=%lu\n", (unsigned long) (void *) &mysleep);
    fflush(stdout);
    while (1) {
        if (libtest(n++)) {
            break;
        }
        libtest_args(10, 20, 30, 40, 50, 60);
        mysleep();
    }

    dlclose(handle);

    return 0;
}

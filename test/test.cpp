#include <stdio.h>
#include <dlfcn.h>
#include <unistd.h>

#define LIBTEST_PATH "libtest.so"

extern "C" bool libtest(int n);  //from libtest.so

int main() {
    void *handle = dlopen(LIBTEST_PATH, RTLD_LAZY);

    if (NULL == handle)
        fprintf(stderr, "Failed to open \"%s\"!\n", LIBTEST_PATH);

    int n = 0;
    while (1) {
        if (libtest(n++)) {
            break;
        }
        sleep(1);
    }

    dlclose(handle);

    return 0;
}

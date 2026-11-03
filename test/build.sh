#! /bin/sh
set -e

g++ -g3 -shared -o libtest.so libtest.cpp -fPIC
g++ -g3 -shared -o libtestnew.so libtestnew.cpp -fPIC

# Prefer a non-PIE test binary so replace/replacep of mysleep covers the far-jump path
# (low executable address -> high .so address). -rdynamic exports mysleep for replace by name.
# Fall back on older toolchains.
if ! g++ -g3 -no-pie -fno-pie -rdynamic -L"$PWD" -Wl,-rpath,'$ORIGIN' -o test test.cpp -ltest -ldl; then
    g++ -g3 -rdynamic -L"$PWD" -Wl,-rpath,'$ORIGIN' -o test test.cpp -ltest -ldl
fi

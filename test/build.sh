#! /bin/sh
set -e

g++ -g3 -shared -o libtest.so libtest.cpp -fPIC
g++ -g3 -shared -o libtestnew.so libtestnew.cpp -fPIC

# Prefer a non-PIE test binary so replacep of mysleep covers the far-jump path
# (low executable address -> high .so address). Fall back on older toolchains.
if ! g++ -g3 -no-pie -fno-pie -L"$PWD" -Wl,-rpath,'$ORIGIN' -o test test.cpp -ltest -ldl; then
    g++ -g3 -L"$PWD" -Wl,-rpath,'$ORIGIN' -o test test.cpp -ltest -ldl
fi

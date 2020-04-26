#! /bin/sh

g++ -g3 -shared -o libtest.so libtest.cpp -fPIC
g++ -g3 -shared -o libtestnew.so libtestnew.cpp -fPIC
g++ -g3 -L$PWD -o test test.cpp -ltest -ldl


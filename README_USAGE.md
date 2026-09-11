# hookso usage

Command reference and walkthrough. Design notes are in the [README](./README.md).

[中文](./README_USAGE_ZH.md)

## Build

```bash
./build.sh
cd test && ./build.sh
```

This produces `hookso`, `test/test`, `test/libtest.so`, and `test/libtestnew.so`.

## Commands

`i=integer` is an integer. `s=string` is copied into the target and passed as a pointer. `@1`–`@6` are only for `trigger` / `triggerp` and mean “Nth argument of the call just caught”.

```text
./hookso syscall pid syscall-number i=int-param1 s="string-param2"
./hookso call pid target-so target-func i=int-param1 s="string-param2"
./hookso dlopen pid target-so-path
./hookso dlclose pid handle
./hookso dlcall pid target-so-path target-func i=int-param1 s="string-param2"
./hookso replace pid src-so src-func target-so-path target-func
./hookso replacep pid func-addr target-so-path target-func
./hookso setfunc pid target-so target-func value
./hookso setfuncp pid func-addr value
./hookso find pid target-so target-func
./hookso arg pid target-so target-func arg-index
./hookso argp pid func-addr arg-index
./hookso trigger pid target-so target-func syscall syscall-number @1 i=int-param2 s="string-param3"
./hookso trigger pid target-so target-func call trigger-so trigger-func @1
./hookso trigger pid target-so target-func dlcall trigger-so trigger-func @1
./hookso trigger pid target-so target-func dlopen target-so-path
./hookso trigger pid target-so target-func dlclose handle
./hookso triggerp pid func-addr syscall syscall-number @1 i=int-param2 s="string-param3"
```

The `so` argument may be a maps basename (`libtest.so`) or a **file path**. If section headers are not mapped, use the path:

```bash
./hookso find 11234 /usr/local/lib64/libstdc++.so.6.0.28 __dynamic_cast
```

syscall / call / dlcall take at most 6 integer or string arguments. `replace` has no arity limit, but the old and new signatures must match.

## Test program

`test.cpp` loops on `libtest` from `libtest.so`. `libtest` calls `puts` three ways (direct, global pointer, local pointer) so relocations differ; see `readelf -r libtest.so`.

```c
int n = 0;
while (1) {
    if (libtest(n++)) {
        break;
    }
    sleep(1);
}
```

```c
typedef int (*PutsFunc)(const char *s);
PutsFunc f = &puts;

extern "C" bool libtest(int n) {
    char buff[128] = {0};
    snprintf(buff, sizeof(buff), "libtest %d", n);
    if (n % 3 == 0) {
        puts(buff);
    } else if (n % 3 == 1) {
        f(buff);
    } else {
        PutsFunc ff = &puts;
        ff(buff);
    }
    return false;
}
```

`libtestnew.so` is not loaded until hookso injects it:

```c
extern "C" bool libtestnew(int n) { /* prints libtestnew N */ }
extern "C" bool putsnew(const char *str) { /* prints putsnew ... */ }
```

```bash
cd test
./build.sh
./test
```

Examples below assume pid `11234`.

## Examples

### 1. Print a string from the target

```bash
./hookso syscall 11234 1 i=1 s="haha" i=4
# 4
```

`4` is the syscall return value. This is `write(1, "haha", 4)`. The target output contains `haha`.

### 2. Call libtest

```bash
./hookso call 11234 libtest.so libtest i=1234
```

The target prints an extra `libtest 1234`.

### 3. Load libtestnew.so

```bash
./hookso dlopen 11234 ./test/libtestnew.so
# 13388992
```

The number is the `dlopen` handle (needed for unload). `libtestnew.so` shows up in `/proc/11234/maps`.

### 4. Unload libtestnew.so

```bash
./hookso dlclose 11234 13388992
```

The same handle is returned for repeated `dlopen`; you must `dlclose` as many times as you opened.

### 5. dlopen + call + dlclose

```bash
./hookso dlcall 11234 ./test/libtestnew.so libtestnew i=1234
```

The target prints `libtestnew 1234`.

### 6. Redirect puts inside libtest.so

```bash
./hookso replace 11234 libtest.so puts ./test/libtestnew.so putsnew
# handle    old-value
```

Calls to `puts` **from libtest.so** become `putsnew`. Other modules are unchanged. This patches GOT/PLT.

### 7. Restore puts

```bash
./hookso setfunc 11234 libtest.so puts 140573454638880
```

Use the old value from example 6. `setfunc` prints the value it overwrote. `libtestnew.so` stays mapped until `dlclose`.

### 8. Jump libtest to libtestnew

```bash
./hookso replace 11234 libtest.so libtest ./test/libtestnew.so libtestnew
```

Unlike example 6, `libtest` is defined in the `.so`, so this writes a `jmp` on `.text`, not a GOT slot. Every call to `libtest` then lands in `libtestnew`.

### 9. Restore libtest

```bash
./hookso setfunc 11234 libtest.so libtest 10442863786053945429
```

### 10. Find an address

```bash
./hookso find 11234 libtest.so libtest
# 0x7fd9cfb91668  140573469644392
```

### 11. Next-call argument

```bash
./hookso arg 11234 libtest.so libtest 1
```

`1` is the first argument. The test loop increments it.

### 12–16. Run something when the function is entered

```bash
./hookso trigger 11234 libtest.so libtest syscall 1 i=1 s="haha" i=4
./hookso trigger 11234 libtest.so libtest call libtest.so libtest @1
./hookso trigger 11234 libtest.so libtest dlcall ./test/libtestnew.so libtestnew @1
./hookso trigger 11234 libtest.so libtest dlopen ./test/libtestnew.so
./hookso trigger 11234 libtest.so libtest dlclose 15367360
```

`@1` forwards the first argument of that `libtest` call.

### 17–18. Same, by address

```bash
./hookso argp 11234 140573469644392 1
./hookso triggerp 11234 140573469644392 syscall 1 i=1 s="haha" i=4
```

### 19–20. Replace by address and restore

```bash
gdb -p 11234 -ex "p (long)libtest" --batch | grep '$1 = ' | awk '{print $3}'
./hookso replacep 11234 4196064 ./test/libtestnew.so libtestnew
# handle  address  old-value
./hookso setfuncp 11234 6295592 140220482557656
```

On the GOT path the address printed by `replacep` may be the GOT slot, not the function you passed in.

### 21. Low address → high `.so` (far jump)

Replace `mysleep` in the main binary with `mysleepnew`. A non-PIE executable at `0x40...` cannot `jmp rel32` to a `.so` at `0x7f...`; hookso stores the pointer on a low page and uses `jmpq *(%rip)`.

```bash
gdb -p 11234 -ex "p (long)mysleep" --batch | grep '$1 = ' | awk '{print $3}'
./hookso replacep 11234 4196356 ./test/libtestnew.so mysleepnew
```

The loop then prints `mysleepnew`.

## Tests

```bash
./build.sh
cd test && ./build.sh && cd ..
bash test/run_tests.sh
```

The tracer must be allowed to ptrace the target. If `kernel.yama.ptrace_scope` is 1, non-child traces can fail:

```bash
sudo sysctl -w kernel.yama.ptrace_scope=0
```

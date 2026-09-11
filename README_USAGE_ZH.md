# hookso 用法说明

命令参考和逐步示例。实现原理见 [README](./README_ZH.md)。

[English](./README_USAGE.md)

## 编译

```bash
./build.sh
cd test && ./build.sh
```

会生成 `hookso`、`test/test`、`test/libtest.so`、`test/libtestnew.so`。

## 命令一览

参数里 `i=整数` 是整型，`s=字符串` 会在目标进程里分配一块内存再传入。`@1`～`@6` 只用在 `trigger` / `triggerp`，表示“刚拦到的那次调用的第 N 个参数”。

```text
# 远程 syscall（下面等价于 write(1, "haha", 4)）
./hookso syscall pid syscall-number i=int-param1 s="string-param2"

# 调用已加载 so 里的函数
./hookso call pid target-so target-func i=int-param1 s="string-param2"

# 注入 / 卸载 so
./hookso dlopen pid target-so-path
./hookso dlclose pid handle

# dlopen + call + dlclose
./hookso dlcall pid target-so-path target-func i=int-param1 s="string-param2"

# 用新 so 的函数替换旧 so 的函数，或替换某个地址
./hookso replace pid src-so src-func target-so-path target-func
./hookso replacep pid func-addr target-so-path target-func

# 按备份值还原
./hookso setfunc pid target-so target-func value
./hookso setfuncp pid func-addr value

# 查地址、读下一次调用的参数
./hookso find pid target-so target-func
./hookso arg pid target-so target-func arg-index
./hookso argp pid func-addr arg-index

# 在目标函数下一次被调用时，再执行 syscall / call / dlcall / dlopen / dlclose
./hookso trigger pid target-so target-func syscall syscall-number @1 i=int-param2 s="string-param3"
./hookso trigger pid target-so target-func call trigger-so trigger-func @1
./hookso trigger pid target-so target-func dlcall trigger-so trigger-func @1
./hookso trigger pid target-so target-func dlopen target-so-path
./hookso trigger pid target-so target-func dlclose handle

# 按地址拦截，参数与 trigger 相同
./hookso triggerp pid func-addr syscall syscall-number @1 i=int-param2 s="string-param3"
```

`so` 参数既可以是 maps 里的文件名（`libtest.so`），也可以是**磁盘路径**。节头没映射进内存时必须用路径，例如：

```bash
./hookso find 11234 /usr/local/lib64/libstdc++.so.6.0.28 __dynamic_cast
```

syscall / call / dlcall 最多 6 个参数，且只能是整数或字符串。`replace` 不限参数个数，但新旧函数签名必须一致。

## 测试程序

`test.cpp` 循环调用 `libtest.so` 的 `libtest`。`libtest` 里用三种写法调用 `puts`（直接调用、函数指针、局部函数指针），对应 ELF 里不同的重定位，方便覆盖 GOT / PLT 替换。可用 `readelf -r libtest.so` 对照。

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

`libtestnew.so` 一开始不会被 test 加载，后面用 hookso 注入：

```c
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
```

```bash
cd test
./build.sh
./test
# libtest 1
# libtest 2
# ...
```

下面假设 test 的 pid 是 `11234`。

## 示例

### 1. 让 test 在屏幕上打印一句话

```bash
./hookso syscall 11234 1 i=1 s="haha" i=4
# 4
```

输出 `4` 是 syscall 返回值。`1` 是 `write` 的系统调用号，整句等价于 `write(1, "haha", 4)`。test 的输出里会出现 `haha`：

```text
libtest 12699
libtest 12700
hahalibtest 12701
```

### 2. 调用 libtest.so 的 libtest

```bash
./hookso call 11234 libtest.so libtest i=1234
# 0
```

test 的输出里会多一次 `libtest 1234`。

### 3. 加载 libtestnew.so

```bash
./hookso dlopen 11234 ./test/libtestnew.so
# 13388992
```

这个数字是 `dlopen` 的 handle，卸载时要用。`/proc/11234/maps` 里会出现 `libtestnew.so`。

### 4. 卸载 libtestnew.so

```bash
./hookso dlclose 11234 13388992
# 13388992
```

多次 `dlopen` 得到的 handle 相同，需要 `dlclose` 同样次数才会真正卸掉。

### 5. dlopen + 调用 + dlclose

```bash
./hookso dlcall 11234 ./test/libtestnew.so libtestnew i=1234
# 0
```

test 输出里会出现 `libtestnew 1234`。这三步等价于示例 3、一次 `call`、示例 4。

### 6. 把 libtest.so 里的 puts 换成 putsnew

```bash
./hookso replace 11234 libtest.so puts ./test/libtestnew.so putsnew
# 13388992    140573454638880
```

第一列是 handle，第二列是替换前的旧值（还原用）。此后 **libtest.so 内部** 的 `puts` 会走到 `putsnew`，进程里其它模块的 `puts` 不变：

```text
putsnew libtest 3318
putsnew libtest 3319
```

这是改 GOT/PLT 的路径。

### 7. 还原 puts

```bash
./hookso setfunc 11234 libtest.so puts 140573454638880
```

`setfunc` 也会打印当前旧值，方便再还原。`libtestnew.so` 此时仍在内存里，不需要的话再 `dlclose`。

### 8. 把 libtest 跳到 libtestnew

```bash
./hookso replace 11234 libtest.so libtest ./test/libtestnew.so libtestnew
# 13388992    10442863786053945429
```

和示例 6 的区别：`libtest` 是 so **内部实现**，走 `.text` 上写 `jmp`，不是改 GOT。之后进程里所有对 `libtest` 的调用都会到 `libtestnew`。

### 9. 还原 libtest

```bash
./hookso setfunc 11234 libtest.so libtest 10442863786053945429
```

### 10. 查函数地址

```bash
./hookso find 11234 libtest.so libtest
# 0x7fd9cfb91668  140573469644392
```

左边是指针，右边是同一个地址的 `uint64_t`。

### 11. 看下一次调用的参数

```bash
./hookso arg 11234 libtest.so libtest 1
# 35
```

最后的 `1` 表示第 1 个参数。test 在循环加一，所以每次看到的值会变。

### 12～16. 函数被调用时再触发动作

```bash
./hookso trigger 11234 libtest.so libtest syscall 1 i=1 s="haha" i=4
./hookso trigger 11234 libtest.so libtest call libtest.so libtest @1
./hookso trigger 11234 libtest.so libtest dlcall ./test/libtestnew.so libtestnew @1
./hookso trigger 11234 libtest.so libtest dlopen ./test/libtestnew.so
./hookso trigger 11234 libtest.so libtest dlclose 15367360
```

`@1` 表示把这次 `libtest` 的第 1 个参数原样传给后面的 call/dlcall。例如 `call` 那条会让 `libtest` 再进一次，输出里同一个数字会出现两遍。

### 17～18. 按地址拦截

`find` 或 gdb 拿到地址后：

```bash
./hookso argp 11234 140573469644392 1
./hookso triggerp 11234 140573469644392 syscall 1 i=1 s="haha" i=4
```

`triggerp` 的后半段参数和 `trigger` 相同。

### 19～20. 按地址替换并还原

```bash
gdb -p 11234 -ex "p (long)libtest" --batch | grep '$1 = ' | awk '{print $3}'
# 4196064
./hookso replacep 11234 4196064 ./test/libtestnew.so libtestnew
# handle  地址  旧值

./hookso setfuncp 11234 6295592 140220482557656
```

`setfuncp` 的地址、旧值用来自 `replacep` 的输出（GOT 路径下地址可能是 GOT 槽，不一定等于你传入的函数地址）。

### 21. 低地址函数跳到高地址 so（远跳）

把 test 自己的 `mysleep` 换成 `libtestnew.so` 的 `mysleepnew`。non-PIE 可执行文件在 `0x40...`，so 在 `0x7f...`，相对偏移超过 `jmp rel32` 的 ±2GB，内部会改走「低地址页存指针 + `jmpq *(%rip)`」。

```bash
gdb -p 11234 -ex "p (long)mysleep" --batch | grep '$1 = ' | awk '{print $3}'
./hookso replacep 11234 4196356 ./test/libtestnew.so mysleepnew
```

之后循环里会打印 `mysleepnew`。

## 测试

```bash
./build.sh
cd test && ./build.sh && cd ..
bash test/run_tests.sh
```

需要能 ptrace 目标进程。若 `kernel.yama.ptrace_scope` 为 1，同用户下非父子关系可能失败，可临时：

```bash
sudo sysctl -w kernel.yama.ptrace_scope=0
```

# hookso

[<img src="https://img.shields.io/github/license/esrrhs/hookso">](https://github.com/esrrhs/hookso)
[<img src="https://img.shields.io/github/languages/top/esrrhs/hookso">](https://github.com/esrrhs/hookso)
[<img src="https://img.shields.io/github/workflow/status/esrrhs/hookso/CI">](https://github.com/esrrhs/hookso/actions)

hookso是一个linux动态链接库的注入修改查找工具，用来修改其他进程的动态链接库行为。

[Readme EN](./README_EN.md)

# 功能
* 让某个进程执行系统调用
* 让某个进程执行.so的某个函数
* 给某个进程挂接新的.so
* 卸载某个进程的.so
* 把旧.so的函数替换为新.so的函数
* 复原.so的函数替换
* 查找.so的函数地址
* 查看.so的函数参数
* 当执行.so的某个函数时，触发执行新的函数

# 编译
git clone代码，运行脚本，生成hookso以及测试程序
```
# ./build.sh  
# cd test && ./build.sh 
```

# 示例
* 启动test目录下的测试程序

先看下测试代码，代码很简单，test.cpp不停的调用libtest.so的libtest函数

```
int n = 0;
while (1) {
    if (libtest(n++)) {
        break;
    }
    sleep(1);
}
```
而libtest.so的libtest函数只是打印到标准输出
```
extern "C" bool libtest(int n) {
    char buff[128] = {0};
    snprintf(buff, sizeof(buff), "libtest %d", n);
    puts(buff);
    return false;
}
```
这时候，test是没有加载libtestnew.so的，后面会用hookso来注入，libtestnew.cpp的代码如下
```
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
libtestnew.cpp定义了两个函数，一个用来替换libtest.so的puts函数，一个用来替换libtest.so的libtest函数

现在我们开始编译并运行它
```
# cd test
# ./build.sh
# ./test
libtest 1
libtest 2
...
libtest 10
```
程序开始运行，可以看到，不停的打印到输出，假设test的pid是11234

* 示例1：让test在屏幕上打印一句话
```
# ./hookso syscall 11234 1 i=1 s="haha" i=4
4
```
注意这里的输出4，表示是系统调用的返回值。然后观察test的输出，可以看到haha输出
```
libtest 12699
libtest 12700
hahalibtest 12701
libtest 12702
libtest 12703
```
这里的几个参数说明：1是系统调用的号码，1表示的是write，i=1意思是一个int类型值为1的参数，s="haha"则为字符串内容为haha

所以这里等价于C语言调用了write(1, "haha", 4)，也就是在标准输出打印一句话

* 示例2：让test调用libtest.so的libtest函数
```
# ./hookso call 11234 libtest.so libtest i=1234
0
```
这里的参数和返回值，和示例1 syscall同理。然后观察test的输出，可以看到输出
```
libtest 12713
libtest 12714
libtest 12715
libtest 1234
libtest 12716
libtest 12717
```
libtest 1234则为我们插入的一次调用输出结果

* 示例3：让test加载libtestnew.so
```
# ./hookso dlopen 11234 ./test/libtestnew.so 
13388992
```
注意这里的输出13388992，表示是dlopen的handle，这个handle后面卸载so会用到。然后查看系统/proc/11234/maps
```
# cat /proc/11234/maps 
00400000-00401000 r-xp 00000000 fc:01 678978                             /home/project/hookso/test/test
00600000-00601000 r--p 00000000 fc:01 678978                             /home/project/hookso/test/test
00601000-00602000 rw-p 00001000 fc:01 678978                             /home/project/hookso/test/test
01044000-01076000 rw-p 00000000 00:00 0                                  [heap]
7fb351aa9000-7fb351aaa000 r-xp 00000000 fc:01 678977                     /home/project/hookso/test/libtestnew.so
7fb351aaa000-7fb351ca9000 ---p 00001000 fc:01 678977                     /home/project/hookso/test/libtestnew.so
7fb351ca9000-7fb351caa000 r--p 00000000 fc:01 678977                     /home/project/hookso/test/libtestnew.so
7fb351caa000-7fb351cab000 rw-p 00001000 fc:01 678977                     /home/project/hookso/test/libtestnew.so
```
可以看到libtestnew.so已经成功加载

* 示例4：让test卸载libtestnew.so
```
# ./hookso dlclose 11234 13388992
13388992
```
这个13388992是示例3 dlopen返回的handle值(多次dlopen的值是一样，并且dlopen多次就得dlclose多次才能真正卸载掉)。然后查看系统/proc/11234/maps
```
# cat /proc/16992/maps 
00400000-00401000 r-xp 00000000 fc:01 678978                             /home/project/hookso/test/test
00600000-00601000 r--p 00000000 fc:01 678978                             /home/project/hookso/test/test
00601000-00602000 rw-p 00001000 fc:01 678978                             /home/project/hookso/test/test
01044000-01076000 rw-p 00000000 00:00 0                                  [heap]
7fb3525ab000-7fb352765000 r-xp 00000000 fc:01 25054                      /usr/lib64/libc-2.17.so
7fb352765000-7fb352964000 ---p 001ba000 fc:01 25054                      /usr/lib64/libc-2.17.so
7fb352964000-7fb352968000 r--p 001b9000 fc:01 25054                      /usr/lib64/libc-2.17.so
7fb352968000-7fb35296a000 rw-p 001bd000 fc:01 25054                      /usr/lib64/libc-2.17.so
```
可以看到已经没用libtestnew.so了

* 示例5：让test加载libtestnew.so，执行libtestnew，然后卸载libtestnew.so
```
# ./hookso dlcall 11234 ./test/libtestnew.so libtestnew i=1234
0
```
同理，这里的输出0为函数返回值。然后观察test的输出，可以看到libtestnew.so的libtestnew函数输出
```
libtest 151
libtest 152
libtest 153
libtestnew 1234
libtest 154
libtest 155
```
libtestnew 1234就是libtestnew.so的函数libtestnew输出，dlcall相当于执行了前面的dlopen、call、dlclose三步操作

* 示例6：让test加载libtestnew.so，并把libtest.so的puts函数调用，修改为调用libtestnew.so的putsnew
```
# ./hookso replace 11234 libtest.so puts ./test/libtestnew.so putsnew
13388992    140573454638880
```
注意这里的输出结果13388992表示handle，140573454638880表示替换之前的旧值，后面我们复原会用到。然后观察test的输出，可以看到已经调用到了libtestnew.so的putsnew方法
```
libtest 3313
libtest 3314
libtest 3315
libtest 3316
libtest 3317
putsnew libtest 3318
putsnew libtest 3319
putsnew libtest 3320
```
现在开始，libtest.so内部调用puts函数，就变成了调用libtestnew.so的putsnew函数了，libtest.so之外调用puts函数，还是以前的没有变

* 示例7：让test的libtest.so的puts函数，恢复到之前，这里的140573454638880就是之前示例6 replace输出的backup旧值
```
# ./hookso setfunc 11234 libtest.so puts 140573454638880
140573442652001
```
注意这里的setfunc也会输出旧值140573442652001，方便下次再还原。然后观察test的输出，可以看到又重新回到了puts方法
```
putsnew libtest 44
putsnew libtest 45
putsnew libtest 46
libtest 47
libtest 48
libtest 49
```
注意这时候libnewtest.so仍然在内存中，如果不需要可以用dlclose卸载它，这里不再赘述

* 示例8：让test加载libtestnew.so，并把libtest.so的libtest函数，跳转到libtestnew的libtestnew，这个和示例6的区别是libtest是libtest.so内部实现的函数，puts是libtest.so调用的外部函数
```
# ./hookso replace 2936 libtest.so libtest ./test/libtestnew.so libtestnew
13388992    10442863786053945429
```
这里的输出和示例6同理。然后观察test的输出，可以看到调用了libtestnew.so的libtestnew函数
```
libtest 31714
libtest 31715
libtest 31716
libtest 31717
libtest 31718
libtestnew 31719
libtestnew 31720
libtestnew 31721
libtestnew 31722
libtestnew 31723
```
现在整个进程所有调用libtest的地方，都跳转到了libtestnew函数

* 示例9：让test的libtest.so的libtest函数，恢复到之前，这里的10442863786053945429就是之前示例8 replace输出的替换旧值
```
# ./hookso setfunc 11234 libtest.so libtest 10442863786053945429
1092601523177
```
然后观察test的输出，可以看到又回到了libtest.so的libtest函数
```
libtestnew 26
libtestnew 27
libtestnew 28
libtestnew 29
libtest 30
libtest 31
libtest 32
```

* 示例10：查找test的libtest.so的libtest函数地址
```
# ./hookso find 11234 libtest.so libtest
0x7fd9cfb91668  140573469644392
```
0x7fd9cfb91668即为地址，140573469644392是地址转成了uint64_t的值

* 示例11：查看libtest.so的libtest的传参值
```
# ./hookso arg 11234 libtest.so libtest 1
35
# ./hookso arg 11234 libtest.so libtest 1
36
```
最后一个参数1表示第1个参数，因为test是在循环+1，所以每次传入libtest函数的参数都在变化

* 示例12：当执行libtest.so的libtest时，执行syscall，在屏幕上输出haha
```
./hookso trigger 11234 libtest.so libtest syscall 1 i=1 s="haha" i=4
4
```
然后观察test的输出，可以看到调用的输出
```
libtest 521
libtest 522
hahalibtest 523
libtest 524
```

* 示例13：当执行libtest.so的libtest时，执行call，用相同的参数调用一次libtest函数
```
./hookso trigger 11234 libtest.so libtest call libtest.so libtest @1
0
```
然后观察test的输出，可以看到输出了两次818
```
libtest 816
libtest 817
libtest 818
libtest 818
libtest 819
libtest 820
```

* 示例14：当执行libtest.so的libtest时，执行dlcall，用相同的参数调用一次libtestnew.so的libtestnew函数
```
./hookso trigger 11234 libtest.so libtest dlcall ./test/libtestnew.so libtestnew @1
0
```
然后观察test的输出，可以看到输出了libtestnew的结果
```
libtest 972
libtest 973
libtestnew 974
libtest 974
libtest 975
```

# QA
##### 为什么就一个2k行+的main.cpp?
因为东西简单，减少无谓的封装，增加可读性
##### 这东西实际有什么作用？
如同瑞士军刀一样，用处好很多。可以用来热更新，或者监控某些函数行为
##### 函数调用有什么限制？
syscall、call、dlcall只支持最大6个参数的函数调用，并且参数只能支持整形、字符  
replace不受限制，但是必须确保新的函数和旧函数，参数一致，不然会core掉
##### 有些so的函数会报错？
某些so太大无法被全部load进内存，导致无法解析，运行失败，如
```
# ./hookso find 11234 libstdc++.so.6.0.28 __dynamic_cast                 
[ERROR][2020.4.28,14:26:55,161]main.cpp:172,remote_process_read: remote_process_read fail 0x7fc375714760 5 Input/output error
```
把so参数修改成文件路径，这样就会从文件读取so信息
```
# ./hookso find 11234 /usr/local/lib64/libstdc++.so.6.0.28 __dynamic_cast
0x7fc37475cea0   140477449227936
```
可以看到，find命令已成功执行，对于其他的命令如call、dlopen、replace同理

# 应用
[Lua的代码覆盖率工具 cLua](https://github.com/esrrhs/cLua)

[Lua 性能分析工具 pLua](https://github.com/esrrhs/pLua)


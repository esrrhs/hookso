# hookso
hookso是一个linux工具，用来修改其他进程的动态链接库行为。

# 功能
* 让某个进程执行系统调用
* 让某个进程执行.so的某个函数
* 给某个进程挂接新的.so
* 卸载某个进程的.so
* 让某个进程挂新的.so并执行某个函数

# 编译
git clone代码，运行脚本，生成hookso以及测试程序
```
# ./build.sh  
# cd test && ./build.sh 
```

# 示例
* 启动test目录下的测试程序
```
# cd test
# ./test
libtest 1
libtest 2
...
libtest 10
```
程序开始运行，不停的打印，假设test的pid是11234

* 示例1：让test在屏幕上打印一句话
```
# ./hookso syscall 11234 1 i=1 s="haha" i=4
```
然后观察test的输出，可以看到haha输出
```
libtest 12699
libtest 12700
hahalibtest 12701
libtest 12702
libtest 12703
```

* 示例2：让test调用libtest.so的libtest函数
```
# ./hookso call 11234 libtest.so libtest i=1234
```
然后观察test的输出，可以看到haha输出
```
libtest 12713
libtest 12714
libtest 12715
libtest 1234
libtest 12716
libtest 12717
```

* 示例3：让test加载libtestnew.so
```
# ./hookso dlopen 11234 ./test/libtestnew.so 
```
然后查看系统/proc/11234/maps
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

* 示例3：让test卸载libtestnew.so
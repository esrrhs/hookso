# hookso

[<img src="https://img.shields.io/github/license/esrrhs/hookso">](https://github.com/esrrhs/hookso)
[<img src="https://img.shields.io/github/languages/top/esrrhs/hookso">](https://github.com/esrrhs/hookso)
[<img src="https://img.shields.io/github/workflow/status/esrrhs/hookso/CI">](https://github.com/esrrhs/hookso/actions)

Hookso is a Linux dynamic link library injection modification search tool, used to modify the dynamic link library behavior of other processes.

# Features
* Let a process execute system calls
* Let a process execute a function of .so
* Attach a new .so to a process
* Uninstall a process of .so
* Replace old .so functions with new .so functions
* Restore .so function replacement
* Find the function address of .so

# Compile
Git clone code, run scripts, generate hookso and test programs
```
# ./build.sh
# cd test && ./build.sh
```

# Example
* Start the test program in the test directory

First look at the test code, the code is very simple, test.cpp constantly calls the libtest function of libtest.so

```
int n = 0;
while (1) {
    if (libtest (n ++)) {
        break;
    }
    sleep (1);
}
```
And the libtest function of libtest.so just prints to standard output
```
extern "C" bool libtest (int n) {
    char buff [128] = {0};
    snprintf (buff, sizeof (buff), "libtest% d", n);
    puts (buff);
    return false;
}
```
At this time, test is not loaded with libtestnew.so, it will be injected with hookso later, the code of libtestnew.cpp is as follows
```
extern "C" bool libtestnew (int n) {
    char buff [128] = {0};
    snprintf (buff, sizeof (buff), "libtestnew% d", n);
    puts (buff);
    return false;
}

extern "C" bool putsnew (const char * str) {
    char buff [128] = {0};
    snprintf (buff, sizeof (buff), "putsnew% s", str);
    puts (buff);
    return false;
}
```
libtestnew.cpp defines two functions, one to replace the puts function of libtest.so and one to replace libtest.so

Now we start to compile and run it
```
# cd test
# ./build.sh
# ./test
libtest 1
libtest 2
...
libtest 10
```
The program starts to run, you can see that it keeps printing to the output, assuming that the test pid is 11234

* Example 1: Let test print a sentence on the screen
```
# ./hookso syscall 11234 1 i = 1 s = "haha" i = 4
4
```
Note that the output 4 here indicates the return value of the system call. Then observe the output of test, you can see the haha ​​output
```
libtest 12699
libtest 12700
hahalibtest 12701
libtest 12702
libtest 12703
```
Here are a few parameter descriptions: 1 is the system call number, 1 means write, i = 1 means a parameter with int type value 1, and s = "haha" means the string content is haha

So here is equivalent to calling write (1, "haha", 4) in C language, which is to print a sentence on the standard output

* Example 2: Let test call the libtest function of libtest.so
```
# ./hookso call 11234 libtest.so libtest i = 1234
0
```
The parameters and return values ​​here are the same as in Example 1 syscall. Then observe the output of test, you can see the output
```
libtest 12713
libtest 12714
libtest 12715
libtest 1234
libtest 12716
libtest 12717
```
libtest 1234 outputs the result for one call we inserted

* Example 3: Let test load libtestnew.so
```
# ./hookso dlopen 11234 ./test/libtestnew.so
13388992
```
Note that the output here is 13388992, which means dlopen's handle, which will be used to uninstall so after this handle. Then check the system / proc / 11234 / maps
```
# cat / proc / 11234 / maps
00400000-00401000 r-xp 00000000 fc: 01 678978 / home / project / hookso / test / test
00600000-00601000 r--p 00000000 fc: 01 678978 / home / project / hookso / test / test
00601000-00602000 rw-p 00001000 fc: 01 678978 / home / project / hookso / test / test
01044000-01076000 rw-p 00000000 00:00 0 [heap]
7fb351aa9000-7fb351aaa000 r-xp 00000000 fc: 01 678977 /home/project/hookso/test/libtestnew.so
7fb351aaa000-7fb351ca9000 --- p 00001000 fc: 01 678977 /home/project/hookso/test/libtestnew.so
7fb351ca9000-7fb351caa000 r--p 00000000 fc: 01 678977 /home/project/hookso/test/libtestnew.so
7fb351caa000-7fb351cab000 rw-p 00001000 fc: 01 678977 /home/project/hookso/test/libtestnew.so
```
You can see that libtestnew.so has been successfully loaded

* Example 4: Let test uninstall libtestnew.so
```
# ./hookso dlclose 11234 13388992
13388992
```
This 13388992 is the handle value returned by dlopen in Example 3 (the value of dlopen is the same for many times, and dlclose must be dlclose multiple times before it can be unloaded) Then check the system / proc / 11234 / maps
```
# cat / proc / 16992 / maps
00400000-00401000 r-xp 00000000 fc: 01 678978 / home / project / hookso / test / test
00600000-00601000 r--p 00000000 fc: 01 678978 / home / project / hookso / test / test
00601000-00602000 rw-p 00001000 fc: 01 678978 / home / project / hookso / test / test
01044000-01076000 rw-p 00000000 00:00 0 [heap]
7fb3525ab000-7fb352765000 r-xp 00000000 fc: 01 25054 /usr/lib64/libc-2.17.so
7fb352765000-7fb352964000 --- p 001ba000 fc: 01 25054 /usr/lib64/libc-2.17.so
7fb352964000-7fb352968000 r--p 001b9000 fc: 01 25054 /usr/lib64/libc-2.17.so
7fb352968000-7fb35296a000 rw-p 001bd000 fc: 01 25054 /usr/lib64/libc-2.17.so
```
You can see that libtestnew.so is useless

* Example 5: Let test load libtestnew.so, execute libtestnew, and then uninstall libtestnew.so
```
# ./hookso dlcall 11234 ./test/libtestnew.so libtestnew i = 1234
0
```
Similarly, the output 0 here is the function return value. Then observe the output of test, you can see the output of libtestnew function of libtestnew.so
```
libtest 151
libtest 152
libtest 153
libtestnew 1234
libtest 154
libtest 155
```
libtestnew 1234 is the function libtestnew output of libtestnew.so, dlcall is equivalent to performing the previous three steps of dlopen, call, dlclose

* Example 6: Let test load libtestnew.so and modify the puts function of libtest.so to call puttestnew of libtestnew.so
```
# ./hookso replace 11234 libtest.so puts ./test/libtestnew.so putsnew
13388992 140573454638880
```
Note that the output here 13388992 represents the handle, and 140573454638880 represents the old value before replacement, which we will use later for restoration. Then observe the output of test, you can see that the putsnew method of libtestnew.so has been called
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
From now on, the call to puts function in libtest.so becomes the call to putsnew function in libtestnew.so. The call to puts function outside libtest.so remains unchanged.
* Example 7: Let the puts function of libtest.so of the test be restored to the previous one, here 140573454638880 is the old backup value output by the previous example 6 replace
```
# ./hookso setfunc 11234 libtest.so puts 140573454638880
140573442652001
```
Note that setfunc here will also output the old value of 140573442652001, so that it can be restored next time. Then observe the output of test, you can see that you have returned to the puts method
```
putsnew libtest 44
putsnew libtest 45
putsnew libtest 46
libtest 47
libtest 48
libtest 49
```
Note that libnewtest.so is still in memory at this time, if you don't need it, you can use dlclose to uninstall it, and I won't repeat it here.

* Example 8: Let test load libtestnew.so and jump the libtest function of libtest.so to libtestnew. The difference between this and example 6 is that libtest is a function implemented inside libtest.so and puts is a call to libtest.so. External function
```
# ./hookso replace 2936 libtest.so libtest ./test/libtestnew.so libtestnew
13388992 10442863786053945429
```
The output here is similar to Example 6. Then observe the output of test, you can see that the libtestnew function of libtestnew.so is called
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
Now all places in the whole process that call libtest jump to the libtestnew function

* Example 9: Let the libtest function of test's libtest.so be restored to the previous one. 10442863786053945429 here is the old value replaced by the previous example 8 replace
```
# ./hookso setfunc 11234 libtest.so libtest 10442863786053945429
1092601523177
```
Then observe the output of test, you can see that it returns to the libtest function of libtest.so
```
libtestnew 26
libtestnew 27
libtestnew 28
libtestnew 29
libtest 30
libtest 31
libtest 32
```

* Example 10: Find the libtest function address of test's libtest.so
```
# ./hookso find 11234 libtest.so libtest
0x7fd9cfb91668 140573469644392
```
0x7fd9cfb91668 is the address, 140573469644392 is the value of the address converted to uint64_t

* Example 11: Find the passed parameter of test libtest.so
```
# ./hookso arg 11234 libtest.so libtest 1
35
# ./hookso arg 11234 libtest.so libtest 1
36
```
The last parameter 1 represents the first parameter, because test is looping +1, so the parameters passed into the libtest function are changing every time

# QA
##### Why is there a main.cpp of 1900+ lines?
Because things are simple, reduce unnecessary packaging, increase readability
##### What does this thing actually do?
The typical usage is to monitor the underlying functions of certain processes, log them, without modifying the original code. Or use it as a hot update patch for c ++
##### What are the limitations of function calls?
syscall, call, and dlcall only support function calls with a maximum of 6 parameters, and the parameters can only support integers and characters
Replace is not limited, but you must ensure that the new function and the old function have the same parameters, otherwise they will core out
##### Some so functions will report errors?
Some so is too large to be fully loaded into memory, resulting in unresolved and failed operation, such as
```
# ./hookso find 11234 libstdc ++. so.6.0.28 __dynamic_cast
[ERROR] [2020.4.28,14: 26: 55,161] main.cpp: 172, remote_process_read: remote_process_read fail 0x7fc375714760 5 Input / output error
```
Modify the so parameter to the file path, so that the so information will be read from the file
```
# ./hookso find 11234 /usr/local/lib64/libstdc++.so.6.0.28 __dynamic_cast
0x7fc37475cea0    140477449227936
```
As you can see, the find command has been successfully executed, the same is true for other commands such as call, dlopen, and replace

# Who is using
[Lua's code coverage tool cLua](https://github.com/esrrhs/cLua)

[Lua performance analysis tool cLua](https://github.com/esrrhs/pLua)

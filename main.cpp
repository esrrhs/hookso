#include <string>
#include <list>
#include <vector>
#include <map>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <typeinfo>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <unordered_map>
#include <fcntl.h>
#include <sstream>
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <set>
#include <unistd.h>
#include <linux/limits.h>
#include <inttypes.h>
#include <elf.h>
#include <sys/uio.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>

#define PROCMAPS_LINE_MAX_LENGTH (PATH_MAX + 100)

#define DEBUG_LOG 1

#ifdef DEBUG_LOG
#define LOG(...) log("[DEBUG] ", __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define ERR(...) log("[ERROR] ", __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#else
#define LOG(...)
#define ERR(...)
#endif

void log(const char *header, const char *file, const char *func, int pos, const char *fmt, ...) {
    FILE *pLog = NULL;
    time_t clock1;
    struct tm *tptr;
    va_list ap;

    pLog = fopen("hookplt.log", "a+");
    if (pLog == NULL) {
        return;
    }

    clock1 = time(0);
    tptr = localtime(&clock1);

    struct timeval tv;
    gettimeofday(&tv, NULL);

    fprintf(pLog, "===========================[%d.%d.%d, %d.%d.%d %llu]%s:%d,%s:===========================\n%s",
            tptr->tm_year + 1990, tptr->tm_mon + 1,
            tptr->tm_mday, tptr->tm_hour, tptr->tm_min,
            tptr->tm_sec, (long long) ((tv.tv_sec) * 1000 + (tv.tv_usec) / 1000), file, pos, func, header);

    va_start(ap, fmt);
    vfprintf(pLog, fmt, ap);
    fprintf(pLog, "\n\n");
    va_end(ap);

    va_start(ap, fmt);
    vprintf(fmt, ap);
    printf("\n\n");
    va_end(ap);

    fclose(pLog);
}

int remote_process_vm_readv(pid_t remote_pid, void *address, void *buffer, size_t len) {
    struct iovec local[1] = {};
    struct iovec remote[1] = {};
    int errsv = 0;
    ssize_t nread = 0;

    local[0].iov_len = len;
    local[0].iov_base = (void *) buffer;

    remote[0].iov_base = address;
    remote[0].iov_len = local[0].iov_len;

    nread = process_vm_readv(remote_pid, local, 1, remote, 1, 0);

    if (nread != (int) local[0].iov_len) {
        errsv = errno;
        return errsv;
    }

    return 0;
}

int remote_process_ptrace_read(pid_t remote_pid, void *address, void *buffer, size_t len) {
    int errsv = 0;

    char file[PATH_MAX];
    sprintf(file, "/proc/%d/mem", remote_pid);
    int fd = open(file, O_RDWR);
    if (fd < 0) {
        errsv = errno;
        return errsv;
    }

    int ret = ptrace(PTRACE_ATTACH, remote_pid, 0, 0);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    ret = waitpid(remote_pid, NULL, 0);
    if (ret < 0) {
        errsv = errno;
        ptrace(PTRACE_DETACH, remote_pid, 0, 0);
        return errsv;
    }

    ret = pread(fd, buffer, len, (off_t) address);
    if (ret < 0) {
        errsv = errno;
        ptrace(PTRACE_DETACH, remote_pid, 0, 0);
        return errsv;
    }

    ret = close(fd);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    ret = ptrace(PTRACE_DETACH, remote_pid, 0, 0);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    return 0;
}

int remote_process_ptrace_word_read(pid_t remote_pid, void *address, void *buffer, size_t len) {
    int errsv = 0;

    int ret = ptrace(PTRACE_ATTACH, remote_pid, 0, 0);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    ret = waitpid(remote_pid, NULL, 0);
    if (ret < 0) {
        errsv = errno;
        ptrace(PTRACE_DETACH, remote_pid, 0, 0);
        return errsv;
    }

    for (int i = 0; i < ((int) len + 3) / 4; ++i) {
        errno = 0;
        ret = ptrace(PTRACE_PEEKTEXT, remote_pid, (char *) address + (i * 4), 0);
        errsv = errno;
        if (errsv != 0) {
            ptrace(PTRACE_DETACH, remote_pid, 0, 0);
            return errsv;
        }
        for (int j = 0; j < 4 && (i * 4) + j < (int) len; ++j) {
            ((char *) buffer)[(i * 4) + j] = ((char *) &ret)[j];
        }
    }

    ret = ptrace(PTRACE_DETACH, remote_pid, 0, 0);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    return 0;
}

int remote_process_read(pid_t remote_pid, void *address, void *buffer, size_t len) {
    int ret = 0;
    ret = remote_process_vm_readv(remote_pid, address, buffer, len);
    if (ret == 0) {
        return ret;
    }
    ret = remote_process_ptrace_read(remote_pid, address, buffer, len);
    if (ret == 0) {
        return ret;
    }
    ret = remote_process_ptrace_word_read(remote_pid, address, buffer, len);
    if (ret == 0) {
        return ret;
    }
    fprintf(stderr, "hookplt: remote_process_read fail %d %s\n", ret, strerror(ret));
    return ret;
}

int find_so_func_addr(pid_t pid, const std::string &soname,
                      const std::string &funcname,
                      uint64_t &funcaddr_plt_offset, void *&funcaddr) {

    char maps_path[PATH_MAX];
    sprintf(maps_path, "/proc/%d/maps", pid);
    FILE *fd = fopen(maps_path, "r");
    if (!fd) {
        fprintf(stderr, "hookplt: cannot open the memory maps, %s\n", strerror(errno));
        return -1;
    }

    std::string sobeginstr;
    char buf[PROCMAPS_LINE_MAX_LENGTH];
    while (!feof(fd)) {
        if (fgets(buf, PROCMAPS_LINE_MAX_LENGTH, fd) == NULL) {
            break;
        }

        std::vector<std::string> tmp;

        const char *sep = "\t \r\n";
        char *line = NULL;
        for (char *token = strtok_r(buf, sep, &line);
             token != NULL;
             token = strtok_r(NULL, ",", &line)) {
            tmp.push_back(token);
        }

        if (tmp.empty()) {
            continue;
        }

        std::string path = tmp[tmp.size() - 1];
        int pos = path.find_last_of("/");
        if (pos == -1) {
            continue;
        }
        std::string targetso = path.substr(pos + 1);
        targetso.erase(std::find_if(targetso.rbegin(), targetso.rend(), [](int ch) {
            return !std::isspace(ch);
        }).base(), targetso.end());
        if (targetso == soname) {
            sobeginstr = tmp[0];
            pos = sobeginstr.find_last_of("-");
            if (pos == -1) {
                fprintf(stderr, "hookplt: parse /proc/%d/maps %s fail\n", pid, soname.c_str());
                return -1;
            }
            sobeginstr = sobeginstr.substr(0, pos);
            break;
        }
    }

    fclose(fd);

    if (sobeginstr.empty()) {
        fprintf(stderr, "hookplt: find /proc/%d/maps %s fail\n", pid, soname.c_str());
        return -1;
    }

    uint64_t sobeginvalue = std::strtoul(sobeginstr.c_str(), 0, 16);

    printf("find target so, begin with 0x%s %lu\n", sobeginstr.c_str(), sobeginvalue);

    Elf64_Ehdr targetso;
    int ret = remote_process_read(pid, (void *) sobeginvalue, &targetso, sizeof(targetso));
    if (ret != 0) {
        return -1;
    }

    if (targetso.e_ident[EI_MAG0] != ELFMAG0 ||
        targetso.e_ident[EI_MAG1] != ELFMAG1 ||
        targetso.e_ident[EI_MAG2] != ELFMAG2 ||
        targetso.e_ident[EI_MAG3] != ELFMAG3) {
        fprintf(stderr, "hookplt: not valid elf header /proc/%d/maps %lu \n", pid, sobeginvalue);
        return -1;
    }

    printf("read head ok %lu\n", sobeginvalue);
    printf("section offset %lu\n", targetso.e_shoff);
    printf("section num %d\n", targetso.e_shnum);
    printf("section size %d\n", targetso.e_shentsize);
    printf("section header string table index %d\n", targetso.e_shstrndx);

    Elf64_Shdr setions[targetso.e_shnum];
    ret = remote_process_read(pid, (void *) (sobeginvalue + targetso.e_shoff), &setions, sizeof(setions));
    if (ret != 0) {
        return -1;
    }

    Elf64_Shdr &shsection = setions[targetso.e_shstrndx];
    printf("section header string table offset %ld\n", shsection.sh_offset);
    printf("section header string table size %ld\n", shsection.sh_size);

    char shsectionname[shsection.sh_size];
    ret = remote_process_read(pid, (void *) (sobeginvalue + shsection.sh_offset), shsectionname, sizeof(shsectionname));
    if (ret != 0) {
        return -1;
    }

    int pltindex = -1;
    int dynsymindex = -1;
    int dynstrindex = -1;
    int relapltindex = -1;
    for (int i = 0; i < targetso.e_shnum; ++i) {
        Elf64_Shdr &s = setions[i];
        std::string name = &shsectionname[s.sh_name];
        if (name == ".plt") {
            pltindex = i;
        }
        if (name == ".dynsym") {
            dynsymindex = i;
        }
        if (name == ".dynstr") {
            dynstrindex = i;
        }
        if (name == ".rela.plt") {
            relapltindex = i;
        }
    }

    if (pltindex < 0) {
        fprintf(stderr, "hookplt: not find .plt %s\n", soname.c_str());
        return -1;
    }
    if (dynsymindex < 0) {
        fprintf(stderr, "hookplt: not find .dynsym %s\n", soname.c_str());
        return -1;
    }
    if (dynstrindex < 0) {
        fprintf(stderr, "hookplt: not find .dynstr %s\n", soname.c_str());
        return -1;
    }
    if (relapltindex < 0) {
        fprintf(stderr, "hookplt: not find .rel.plt %s\n", soname.c_str());
        return -1;
    }

    Elf64_Shdr &pltsection = setions[pltindex];
    printf("plt index %d\n", pltindex);
    printf("plt section offset %ld\n", pltsection.sh_offset);
    printf("plt section size %ld\n", pltsection.sh_size);

    Elf64_Shdr &dynsymsection = setions[dynsymindex];
    printf("dynsym index %d\n", dynsymindex);
    printf("dynsym section offset %ld\n", dynsymsection.sh_offset);
    printf("dynsym section size %ld\n", dynsymsection.sh_size / sizeof(Elf64_Sym));

    Elf64_Sym sym[dynsymsection.sh_size / sizeof(Elf64_Sym)];
    ret = remote_process_read(pid, (void *) (sobeginvalue + dynsymsection.sh_offset), &sym, sizeof(sym));
    if (ret != 0) {
        return -1;
    }

    Elf64_Shdr &dynstrsection = setions[dynstrindex];
    printf("dynstr index %d\n", dynstrindex);
    printf("dynstr section offset %ld\n", dynstrsection.sh_offset);
    printf("dynstr section size %ld\n", dynstrsection.sh_size);

    char dynstr[dynstrsection.sh_size];
    ret = remote_process_read(pid, (void *) (sobeginvalue + dynstrsection.sh_offset), dynstr, sizeof(dynstr));
    if (ret != 0) {
        return -1;
    }

    int symfuncindex = -1;
    for (int j = 0; j < (int) (dynsymsection.sh_size / sizeof(Elf64_Sym)); ++j) {
        Elf64_Sym &s = sym[j];
        std::string name = &dynstr[s.st_name];
        if (name == funcname) {
            symfuncindex = j;
            break;
        }
    }

    if (symfuncindex < 0) {
        fprintf(stderr, "hookplt: not find %s in .dynsym %s\n", funcname.c_str(), soname.c_str());
        return -1;
    }

    Elf64_Sym &targetsym = sym[symfuncindex];
    if (targetsym.st_shndx != SHN_UNDEF && targetsym.st_value != 0 && targetsym.st_size != 0) {
        Elf64_Shdr &s = setions[targetsym.st_shndx];
        std::string name = &shsectionname[s.sh_name];
        if (name == ".text") {
            void *func = (void *) (sobeginvalue + targetsym.st_value);
            printf("target text func addr %p\n", func);
            funcaddr_plt_offset = 0;
            funcaddr = func;
            return 0;
        }
    }

    Elf64_Shdr &relapltsection = setions[relapltindex];
    printf("relaplt index %d\n", relapltindex);
    printf("relaplt section offset %ld\n", relapltsection.sh_offset);
    printf("relaplt section size %ld\n", relapltsection.sh_size / sizeof(Elf64_Rela));

    Elf64_Rela rela[relapltsection.sh_size / sizeof(Elf64_Rela)];
    ret = remote_process_read(pid, (void *) (sobeginvalue + relapltsection.sh_offset), &rela, sizeof(rela));
    if (ret != 0) {
        return -1;
    }

    int relafuncindex = -1;
    for (int j = 0; j < (int) (relapltsection.sh_size / sizeof(Elf64_Rela)); ++j) {
        Elf64_Rela &r = rela[j];
        if ((int) ELF64_R_SYM(r.r_info) == symfuncindex) {
            relafuncindex = j;
            break;
        }
    }

    if (relafuncindex < 0) {
        fprintf(stderr, "hookplt: not find %s in .rela.plt %s\n", funcname.c_str(), soname.c_str());
        return -1;
    }

    Elf64_Rela &relafunc = rela[relafuncindex];
    printf("target rela index %d\n", relafuncindex);
    printf("target rela addr %ld\n", relafunc.r_offset);

    void *func;
    ret = remote_process_read(pid, (void *) (sobeginvalue + relafunc.r_offset), &func, sizeof(func));
    if (ret != 0) {
        return -1;
    }

    printf("target got.plt func old addr %p\n", func);

    funcaddr_plt_offset = relafunc.r_offset;
    funcaddr = func;

    return 0;
}

int main(int argc, char **argv) {

    if (argc < 3) {
        fprintf(stderr, "hookplt: pid target.so target.func\n");
        return 1;
    }

    printf("pid=%s\n", argv[1]);
    printf("target so=%s\n", argv[2]);
    printf("target function=%s\n", argv[3]);

    printf("start parse so file %s %s\n", argv[2], argv[3]);

    int pid = atoi(argv[1]);

    uint64_t old_funcaddr_plt_offset = 0;
    void *old_funcaddr = 0;
    int ret = find_so_func_addr(pid, argv[2], argv[3], old_funcaddr_plt_offset, old_funcaddr);
    if (ret != 0) {
        return 1;
    }

    printf("%s old %s %p offset %lu\n", argv[2], argv[3], old_funcaddr, old_funcaddr_plt_offset);

    return 0;
}

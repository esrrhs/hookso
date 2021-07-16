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
#include <sys/user.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <sys/stat.h>

#define PROCMAPS_LINE_MAX_LENGTH (PATH_MAX + 100)

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

//#define DEBUG_LOG

#ifdef DEBUG_LOG
#define LOG(...) log(stdout, "[DEBUG]", __FILENAME__, __FUNCTION__, __LINE__, __VA_ARGS__)
#else
#define LOG(...)
#endif
#define ERR(...) log(stderr, "[ERROR]", __FILENAME__, __FUNCTION__, __LINE__, __VA_ARGS__)

void log(FILE *fd, const char *header, const char *file, const char *func, int pos, const char *fmt, ...) {
    time_t clock1;
    struct tm *tptr;
    va_list ap;

    clock1 = time(0);
    tptr = localtime(&clock1);

    struct timeval tv;
    gettimeofday(&tv, NULL);

    fprintf(fd, "%s[%d.%d.%d,%d:%d:%d,%llu]%s:%d,%s: ", header,
            tptr->tm_year + 1900, tptr->tm_mon + 1,
            tptr->tm_mday, tptr->tm_hour, tptr->tm_min,
            tptr->tm_sec, (long long) ((tv.tv_usec) / 1000) % 1000, file, pos, func);

    va_start(ap, fmt);
    vfprintf(fd, fmt, ap);
    fprintf(fd, "\n");
    va_end(ap);
}

int remote_process_vm_readv(int remote_pid, void *address, void *buffer, size_t len) {
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

int remote_process_ptrace_read(int remote_pid, void *address, void *buffer, size_t len) {
    int errsv = 0;

    char file[PATH_MAX];
    sprintf(file, "/proc/%d/mem", remote_pid);
    int fd = open(file, O_RDWR);
    if (fd < 0) {
        errsv = errno;
        return errsv;
    }

    int ret = pread(fd, buffer, len, (off_t) address);
    if (ret < 0) {
        errsv = errno;
        close(fd);
        return errsv;
    }

    ret = close(fd);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    return 0;
}

int remote_process_ptrace_word_read(int remote_pid, void *address, void *buffer, size_t len) {
    char *dest = (char *) buffer;
    char *addr = (char *) address;
    while (len >= sizeof(long)) {
        errno = 0;
        long data = ptrace(PTRACE_PEEKTEXT, remote_pid, addr, 0);
        if (data == -1 && errno != 0) {
            return errno;
        }
        *(long *) dest = data;
        addr += sizeof(long);
        dest += sizeof(long);
        len -= sizeof(long);
    }
    if (len != 0) {
        long data = 0;
        char *src = (char *) &data;
        errno = 0;
        data = ptrace(PTRACE_PEEKTEXT, remote_pid, addr, 0);
        if (data == -1 && errno != 0) {
            return errno;
        }
        while (len--) {
            *(dest++) = *(src++);
        }
    }
    return 0;
}

int remote_process_read(int remote_pid, void *address, void *buffer, size_t len, bool silent = false) {
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

    if (!silent) {
        ERR("remote_process_read fail %p %d %s", address, ret, strerror(ret));
    }
    return ret;
}

int remote_process_vm_writev(int remote_pid, void *address, void *buffer, size_t len) {
    struct iovec local[1] = {};
    struct iovec remote[1] = {};
    int errsv = 0;
    ssize_t nread = 0;

    local[0].iov_len = len;
    local[0].iov_base = (void *) buffer;

    remote[0].iov_base = address;
    remote[0].iov_len = local[0].iov_len;

    nread = process_vm_writev(remote_pid, local, 1, remote, 1, 0);

    if (nread != (int) local[0].iov_len) {
        errsv = errno;
        return errsv;
    }

    return 0;
}

int remote_process_ptrace_write(int remote_pid, void *address, void *buffer, size_t len) {
    int errsv = 0;

    char file[PATH_MAX];
    sprintf(file, "/proc/%d/mem", remote_pid);
    int fd = open(file, O_RDWR);
    if (fd < 0) {
        errsv = errno;
        return errsv;
    }

    int ret = pwrite(fd, buffer, len, (off_t) address);
    if (ret < 0) {
        errsv = errno;
        close(fd);
        return errsv;
    }

    ret = close(fd);
    if (ret < 0) {
        errsv = errno;
        return errsv;
    }

    return 0;
}

int remote_process_ptrace_word_write(int remote_pid, void *address, void *buffer, size_t len) {
    const char *src = (const char *) buffer;
    char *addr = (char *) address;
    while (len >= sizeof(long)) {
        int ret = ptrace(PTRACE_POKETEXT, remote_pid, addr, *(long *) src);
        if (ret != 0) {
            return errno;
        }
        addr += sizeof(long);
        src += sizeof(long);
        len -= sizeof(long);
    }
    if (len != 0) {
        long data = ptrace(PTRACE_PEEKTEXT, remote_pid, addr, 0);
        char *dest = (char *) &data;
        if (data == -1 && errno != 0) {
            return errno;
        }
        while (len--) {
            *(dest++) = *(src++);
        }
        int ret = ptrace(PTRACE_POKETEXT, remote_pid, addr, data);
        if (ret != 0) {
            return errno;
        }
    }
    return 0;
}

int remote_process_write(int remote_pid, void *address, void *buffer, size_t len) {
    int ret = 0;
    ret = remote_process_vm_writev(remote_pid, address, buffer, len);
    if (ret == 0) {
        return ret;
    }
    ret = remote_process_ptrace_write(remote_pid, address, buffer, len);
    if (ret == 0) {
        return ret;
    }
    ret = remote_process_ptrace_word_write(remote_pid, address, buffer, len);
    if (ret == 0) {
        return ret;
    }
    ERR("remote_process_write fail %p %d %s", address, ret, strerror(ret));
    return ret;
}

template<typename T>
class arrayholder {
public:
    arrayholder(int n) {
        m_p = (T *) malloc(sizeof(T) * n);
        m_n = n;
    }

    ~arrayholder() {
        free(m_p);
    }

    T &operator[](int n) {
        return m_p[n];
    }

    T *operator&() {
        return m_p;
    }

    size_t size() {
        return m_n * sizeof(T);
    }

private:
    T *m_p;
    int m_n;
};

int find_so_func_addr_by_mem(int pid, const std::string &soname,
                             const std::string &funcname,
                             std::vector<void *> &funcaddr_plt, void *&funcaddr) {

    char maps_path[PATH_MAX];
    sprintf(maps_path, "/proc/%d/maps", pid);
    FILE *fd = fopen(maps_path, "r");
    if (!fd) {
        ERR("cannot open the memory maps, %s", strerror(errno));
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
        for (char *token = strtok_r(buf, sep, &line); token != NULL; token = strtok_r(NULL, sep, &line)) {
            tmp.push_back(token);
        }

        if (tmp.empty()) {
            continue;
        }

        std::string path = tmp[tmp.size() - 1];
        if (path == "(deleted)") {
            if (tmp.size() < 2) {
                continue;
            }
            path = tmp[tmp.size() - 2];
        }

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
                fclose(fd);
                ERR("parse /proc/%d/maps %s fail", pid, soname.c_str());
                return -1;
            }
            sobeginstr = sobeginstr.substr(0, pos);
            break;
        }
    }

    fclose(fd);

    if (sobeginstr.empty()) {
        ERR("find /proc/%d/maps %s fail", pid, soname.c_str());
        return -1;
    }

    uint64_t sobeginvalue = std::strtoul(sobeginstr.c_str(), 0, 16);

    LOG("find target so, begin with 0x%s %lu", sobeginstr.c_str(), sobeginvalue);

    Elf64_Ehdr targetso;
    int ret = remote_process_read(pid, (void *) sobeginvalue, &targetso, sizeof(targetso));
    if (ret != 0) {
        return -1;
    }

    if (targetso.e_ident[EI_MAG0] != ELFMAG0 ||
        targetso.e_ident[EI_MAG1] != ELFMAG1 ||
        targetso.e_ident[EI_MAG2] != ELFMAG2 ||
        targetso.e_ident[EI_MAG3] != ELFMAG3) {
        ERR("not valid elf header /proc/%d/maps %lu ", pid, sobeginvalue);
        return -1;
    }

    LOG("read head ok %lu", sobeginvalue);
    LOG("section offset %lu", targetso.e_shoff);
    LOG("section num %d", targetso.e_shnum);
    LOG("section size %d", targetso.e_shentsize);
    LOG("section header string table index %d", targetso.e_shstrndx);

    arrayholder<Elf64_Shdr> setions(targetso.e_shnum);
    ret = remote_process_read(pid, (void *) (sobeginvalue + targetso.e_shoff), &setions, setions.size());
    if (ret != 0) {
        return -1;
    }

    Elf64_Shdr &shsection = setions[targetso.e_shstrndx];
    LOG("section header string table offset %ld", shsection.sh_offset);
    LOG("section header string table size %ld", shsection.sh_size);

    arrayholder<char> shsectionname(shsection.sh_size);
    ret = remote_process_read(pid, (void *) (sobeginvalue + shsection.sh_offset), &shsectionname, shsectionname.size());
    if (ret != 0) {
        return -1;
    }

    int pltindex = -1;
    int dynsymindex = -1;
    int dynstrindex = -1;
    int relapltindex = -1;
    int reladynindex = -1;
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
        if (name == ".rela.dyn") {
            reladynindex = i;
        }
    }

    if (pltindex < 0) {
        ERR("not find .plt %s", soname.c_str());
        return -1;
    }
    if (dynsymindex < 0) {
        ERR("not find .dynsym %s", soname.c_str());
        return -1;
    }
    if (dynstrindex < 0) {
        ERR("not find .dynstr %s", soname.c_str());
        return -1;
    }
    if (relapltindex < 0) {
        ERR("not find .rel.plt %s", soname.c_str());
        return -1;
    }
    if (reladynindex < 0) {
        ERR("not find .rela.dyn %s", soname.c_str());
        return -1;
    }

    Elf64_Shdr &pltsection = setions[pltindex];
    LOG("plt index %d", pltindex);
    LOG("plt section offset %ld", pltsection.sh_offset);
    LOG("plt section size %ld", pltsection.sh_size);

    Elf64_Shdr &dynsymsection = setions[dynsymindex];
    LOG("dynsym index %d", dynsymindex);
    LOG("dynsym section offset %ld", dynsymsection.sh_offset);
    LOG("dynsym section size %ld", dynsymsection.sh_size / sizeof(Elf64_Sym));

    arrayholder<Elf64_Sym> sym(dynsymsection.sh_size / sizeof(Elf64_Sym));
    ret = remote_process_read(pid, (void *) (sobeginvalue + dynsymsection.sh_offset), &sym, sym.size());
    if (ret != 0) {
        return -1;
    }

    Elf64_Shdr &dynstrsection = setions[dynstrindex];
    LOG("dynstr index %d", dynstrindex);
    LOG("dynstr section offset %ld", dynstrsection.sh_offset);
    LOG("dynstr section size %ld", dynstrsection.sh_size);

    arrayholder<char> dynstr(dynstrsection.sh_size);
    ret = remote_process_read(pid, (void *) (sobeginvalue + dynstrsection.sh_offset), &dynstr, dynstr.size());
    if (ret != 0) {
        return -1;
    }

    int symfuncindex = -1;
    for (int j = 0; j < (int) (dynsymsection.sh_size / sizeof(Elf64_Sym)); ++j) {
        Elf64_Sym &s = sym[j];
        std::string name = &dynstr[s.st_name];
        if (name == funcname && (ELF64_ST_TYPE(s.st_info) == STT_FUNC || ELF64_ST_TYPE(s.st_info) == STT_NOTYPE)) {
            symfuncindex = j;
            break;
        }
    }

    if (symfuncindex < 0) {
        ERR("not find %s in .dynsym %s", funcname.c_str(), soname.c_str());
        return -1;
    }

    Elf64_Sym &targetsym = sym[symfuncindex];
    if (targetsym.st_shndx != SHN_UNDEF && targetsym.st_value != 0 && targetsym.st_size != 0) {
        Elf64_Shdr &s = setions[targetsym.st_shndx];
        std::string name = &shsectionname[s.sh_name];
        if (name == ".text") {
            void *func = (void *) (sobeginvalue + targetsym.st_value);
            LOG("target text func addr %p", func);
            funcaddr_plt.clear();
            funcaddr = func;
            return 0;
        }
    }

    Elf64_Shdr &relapltsection = setions[relapltindex];
    LOG("relaplt index %d", relapltindex);
    LOG("relaplt section offset %ld", relapltsection.sh_offset);
    LOG("relaplt section size %ld", relapltsection.sh_size / sizeof(Elf64_Rela));

    arrayholder<Elf64_Rela> rela(relapltsection.sh_size / sizeof(Elf64_Rela));
    ret = remote_process_read(pid, (void *) (sobeginvalue + relapltsection.sh_offset), &rela, rela.size());
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

    Elf64_Shdr &reladynsection = setions[reladynindex];
    LOG("reladyn index %d", reladynindex);
    LOG("reladyn section offset %ld", reladynsection.sh_offset);
    LOG("reladyn section size %ld", reladynsection.sh_size / sizeof(Elf64_Rela));

    arrayholder<Elf64_Rela> reladyn(reladynsection.sh_size / sizeof(Elf64_Rela));
    ret = remote_process_read(pid, (void *) (sobeginvalue + reladynsection.sh_offset), &reladyn, reladyn.size());
    if (ret != 0) {
        return -1;
    }

    std::vector<int> reladynfuncindexs;
    for (int j = 0; j < (int) (reladynsection.sh_size / sizeof(Elf64_Rela)); ++j) {
        Elf64_Rela &r = reladyn[j];
        if ((int) ELF64_R_SYM(r.r_info) == symfuncindex &&
            (ELF64_R_TYPE(r.r_info) == R_X86_64_64 || ELF64_R_TYPE(r.r_info) == R_X86_64_GLOB_DAT)) {
            reladynfuncindexs.push_back(j);
        }
    }

    if (relafuncindex < 0 && reladynfuncindexs.size() <= 0) {
        ERR("not find %s in .rela.plt or .rela.dyn %s", funcname.c_str(), soname.c_str());
        return -1;
    }

    if (relafuncindex >= 0) {
        Elf64_Rela &relafunc = rela[relafuncindex];
        LOG("target rela index %d", relafuncindex);
        LOG("target rela addr %ld", relafunc.r_offset);

        void *func;
        ret = remote_process_read(pid, (void *) (sobeginvalue + relafunc.r_offset), &func, sizeof(func));
        if (ret != 0) {
            return -1;
        }

        LOG("target got.plt func old addr %p", func);

        funcaddr_plt.push_back((void *) (sobeginvalue + relafunc.r_offset));
        funcaddr = func;
    }

    if (reladynfuncindexs.size() > 0) {
        for (int i = 0; i < (int) reladynfuncindexs.size(); ++i) {
            Elf64_Rela &relafunc = reladyn[reladynfuncindexs[i]];
            LOG("target rela index %d", relafuncindex);
            LOG("target rela addr %ld", relafunc.r_offset);

            void *func;
            ret = remote_process_read(pid, (void *) (sobeginvalue + relafunc.r_offset), &func, sizeof(func));
            if (ret != 0) {
                return -1;
            }

            if (funcaddr == 0) {
                funcaddr = func;
            } else {
                if (funcaddr != func) {
                    ERR("diff func addr %s in .rela.plt .rela.dyn %s", funcname.c_str(), soname.c_str());
                    return -1;
                }
            }

            funcaddr_plt.push_back((void *) (sobeginvalue + relafunc.r_offset));
        }
    }

    return 0;
}

int find_so_func_addr_by_file(int pid, const std::string &targetsopath,
                              const std::string &funcname,
                              std::vector<void *> &funcaddr_plt, void *&funcaddr, int sofd) {
    std::string soname = targetsopath;
    int pos = targetsopath.find_last_of("/");
    if (pos != -1) {
        soname = targetsopath.substr(pos + 1);
    }
    soname.erase(std::find_if(soname.rbegin(), soname.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), soname.end());

    char maps_path[PATH_MAX];
    sprintf(maps_path, "/proc/%d/maps", pid);
    FILE *fd = fopen(maps_path, "r");
    if (!fd) {
        ERR("cannot open the memory maps, %s", strerror(errno));
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
        for (char *token = strtok_r(buf, sep, &line); token != NULL; token = strtok_r(NULL, sep, &line)) {
            tmp.push_back(token);
        }

        if (tmp.empty()) {
            continue;
        }

        std::string path = tmp[tmp.size() - 1];
        if (path == "(deleted)") {
            if (tmp.size() < 2) {
                continue;
            }
            path = tmp[tmp.size() - 2];
        }

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
                fclose(fd);
                ERR("parse /proc/%d/maps %s fail", pid, soname.c_str());
                return -1;
            }
            sobeginstr = sobeginstr.substr(0, pos);
            break;
        }
    }

    fclose(fd);

    if (sobeginstr.empty()) {
        ERR("find /proc/%d/maps %s fail", pid, soname.c_str());
        return -1;
    }

    uint64_t sobeginvalue = std::strtoul(sobeginstr.c_str(), 0, 16);

    LOG("find target so, begin with 0x%s %lu", sobeginstr.c_str(), sobeginvalue);

    Elf64_Ehdr targetso;
    int ret = remote_process_read(pid, (void *) sobeginvalue, &targetso, sizeof(targetso));
    if (ret != 0) {
        return -1;
    }

    if (targetso.e_ident[EI_MAG0] != ELFMAG0 ||
        targetso.e_ident[EI_MAG1] != ELFMAG1 ||
        targetso.e_ident[EI_MAG2] != ELFMAG2 ||
        targetso.e_ident[EI_MAG3] != ELFMAG3) {
        ERR("not valid elf header /proc/%d/maps %lu ", pid, sobeginvalue);
        return -1;
    }

    LOG("read head ok %lu", sobeginvalue);
    LOG("section offset %lu", targetso.e_shoff);
    LOG("section num %d", targetso.e_shnum);
    LOG("section size %d", targetso.e_shentsize);
    LOG("section header string table index %d", targetso.e_shstrndx);

    struct stat st;
    ret = fstat(sofd, &st);
    if (ret < 0) {
        ERR("fstat fail /proc/%d/maps %s", pid, targetsopath.c_str());
        return -1;
    }

    int sofilelen = st.st_size;
    LOG("so file len %s %d", targetsopath.c_str(), sofilelen);

    char *sofileaddr = (char *) mmap(NULL, sofilelen, PROT_READ, MAP_PRIVATE, sofd, 0);
    if (sofileaddr == MAP_FAILED) {
        ERR("mmap fail /proc/%d/maps %s", pid, targetsopath.c_str());
        return -1;
    }

    if (memcmp(sofileaddr, &targetso, sizeof(targetso)) != 0) {
        munmap(sofileaddr, sofilelen);
        ERR("mmap diff /proc/%d/maps %s", pid, targetsopath.c_str());
        return -1;
    }

    arrayholder<Elf64_Shdr> setions(targetso.e_shnum);
    memcpy(&setions, sofileaddr + targetso.e_shoff, setions.size());

    Elf64_Shdr &shsection = setions[targetso.e_shstrndx];
    LOG("section header string table offset %ld", shsection.sh_offset);
    LOG("section header string table size %ld", shsection.sh_size);

    arrayholder<char> shsectionname(shsection.sh_size);
    memcpy(&shsectionname, sofileaddr + shsection.sh_offset, shsectionname.size());

    int pltindex = -1;
    int dynsymindex = -1;
    int dynstrindex = -1;
    int relapltindex = -1;
    int reladynindex = -1;
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
        if (name == ".rela.dyn") {
            reladynindex = i;
        }
    }

    if (pltindex < 0) {
        munmap(sofileaddr, sofilelen);
        ERR("not find .plt %s", soname.c_str());
        return -1;
    }
    if (dynsymindex < 0) {
        munmap(sofileaddr, sofilelen);
        ERR("not find .dynsym %s", soname.c_str());
        return -1;
    }
    if (dynstrindex < 0) {
        munmap(sofileaddr, sofilelen);
        ERR("not find .dynstr %s", soname.c_str());
        return -1;
    }
    if (relapltindex < 0) {
        munmap(sofileaddr, sofilelen);
        ERR("not find .rel.plt %s", soname.c_str());
        return -1;
    }
    if (reladynindex < 0) {
        munmap(sofileaddr, sofilelen);
        ERR("not find .rela.dyn %s", soname.c_str());
        return -1;
    }

    Elf64_Shdr &pltsection = setions[pltindex];
    LOG("plt index %d", pltindex);
    LOG("plt section offset %ld", pltsection.sh_offset);
    LOG("plt section size %ld", pltsection.sh_size);

    Elf64_Shdr &dynsymsection = setions[dynsymindex];
    LOG("dynsym index %d", dynsymindex);
    LOG("dynsym section offset %ld", dynsymsection.sh_offset);
    LOG("dynsym section size %ld", dynsymsection.sh_size / sizeof(Elf64_Sym));

    arrayholder<Elf64_Sym> sym(dynsymsection.sh_size / sizeof(Elf64_Sym));
    memcpy(&sym, sofileaddr + dynsymsection.sh_offset, sym.size());

    Elf64_Shdr &dynstrsection = setions[dynstrindex];
    LOG("dynstr index %d", dynstrindex);
    LOG("dynstr section offset %ld", dynstrsection.sh_offset);
    LOG("dynstr section size %ld", dynstrsection.sh_size);

    arrayholder<char> dynstr(dynstrsection.sh_size);
    memcpy(&dynstr, sofileaddr + dynstrsection.sh_offset, dynstr.size());

    int symfuncindex = -1;
    for (int j = 0; j < (int) (dynsymsection.sh_size / sizeof(Elf64_Sym)); ++j) {
        Elf64_Sym &s = sym[j];
        std::string name = &dynstr[s.st_name];
        if (name == funcname && (ELF64_ST_TYPE(s.st_info) == STT_FUNC || ELF64_ST_TYPE(s.st_info) == STT_NOTYPE)) {
            symfuncindex = j;
            break;
        }
    }

    if (symfuncindex < 0) {
        munmap(sofileaddr, sofilelen);
        ERR("not find %s in .dynsym %s", funcname.c_str(), soname.c_str());
        return -1;
    }

    Elf64_Sym &targetsym = sym[symfuncindex];
    if (targetsym.st_shndx != SHN_UNDEF && targetsym.st_value != 0 && targetsym.st_size != 0) {
        Elf64_Shdr &s = setions[targetsym.st_shndx];
        std::string name = &shsectionname[s.sh_name];
        if (name == ".text") {
            munmap(sofileaddr, sofilelen);
            void *func = (void *) (sobeginvalue + targetsym.st_value);
            LOG("target text func addr %p", func);
            funcaddr_plt.clear();
            funcaddr = func;
            return 0;
        }
    }

    Elf64_Shdr &relapltsection = setions[relapltindex];
    LOG("relaplt index %d", relapltindex);
    LOG("relaplt section offset %ld", relapltsection.sh_offset);
    LOG("relaplt section size %ld", relapltsection.sh_size / sizeof(Elf64_Rela));

    arrayholder<Elf64_Rela> rela(relapltsection.sh_size / sizeof(Elf64_Rela));
    memcpy(&rela, sofileaddr + relapltsection.sh_offset, rela.size());

    int relafuncindex = -1;
    for (int j = 0; j < (int) (relapltsection.sh_size / sizeof(Elf64_Rela)); ++j) {
        Elf64_Rela &r = rela[j];
        if ((int) ELF64_R_SYM(r.r_info) == symfuncindex) {
            relafuncindex = j;
            break;
        }
    }

    Elf64_Shdr &reladynsection = setions[reladynindex];
    LOG("reladyn index %d", reladynindex);
    LOG("reladyn section offset %ld", reladynsection.sh_offset);
    LOG("reladyn section size %ld", reladynsection.sh_size / sizeof(Elf64_Rela));

    arrayholder<Elf64_Rela> reladyn(reladynsection.sh_size / sizeof(Elf64_Rela));
    memcpy(&reladyn, sofileaddr + reladynsection.sh_offset, reladyn.size());

    std::vector<int> reladynfuncindexs;
    for (int j = 0; j < (int) (reladynsection.sh_size / sizeof(Elf64_Rela)); ++j) {
        Elf64_Rela &r = reladyn[j];
        if ((int) ELF64_R_SYM(r.r_info) == symfuncindex &&
            (ELF64_R_TYPE(r.r_info) == R_X86_64_64 || ELF64_R_TYPE(r.r_info) == R_X86_64_GLOB_DAT)) {
            reladynfuncindexs.push_back(j);
        }
    }

    if (relafuncindex < 0 && reladynfuncindexs.size() <= 0) {
        munmap(sofileaddr, sofilelen);
        ERR("not find %s in .rela.plt or .rela.dyn %s", funcname.c_str(), soname.c_str());
        return -1;
    }

    if (relafuncindex >= 0) {
        Elf64_Rela &relafunc = rela[relafuncindex];
        LOG("target rela index %d", relafuncindex);
        LOG("target rela addr %ld", relafunc.r_offset);

        void *func;
        ret = remote_process_read(pid, (void *) (sobeginvalue + relafunc.r_offset), &func, sizeof(func));
        if (ret != 0) {
            munmap(sofileaddr, sofilelen);
            return -1;
        }

        LOG("target got.plt func old addr %p", func);

        funcaddr_plt.push_back((void *) (sobeginvalue + relafunc.r_offset));
        funcaddr = func;
    }

    if (reladynfuncindexs.size() > 0) {
        for (int i = 0; i < (int) reladynfuncindexs.size(); ++i) {
            Elf64_Rela &relafunc = reladyn[reladynfuncindexs[i]];
            LOG("target rela index %d", relafuncindex);
            LOG("target rela addr %ld", relafunc.r_offset);

            void *func;
            ret = remote_process_read(pid, (void *) (sobeginvalue + relafunc.r_offset), &func, sizeof(func));
            if (ret != 0) {
                munmap(sofileaddr, sofilelen);
                return -1;
            }

            if (funcaddr == 0) {
                funcaddr = func;
            } else {
                if (funcaddr != func) {
                    munmap(sofileaddr, sofilelen);
                    ERR("diff func addr %s in .rela.plt .rela.dyn %s", funcname.c_str(), soname.c_str());
                    return -1;
                }
            }

            funcaddr_plt.push_back((void *) (sobeginvalue + relafunc.r_offset));
        }
    }

    munmap(sofileaddr, sofilelen);

    return 0;
}

int find_so_func_addr(int pid, const std::string &so,
                      const std::string &funcname,
                      std::vector<void *> &funcaddr_plt, void *&funcaddr) {

    int sofd = open(so.c_str(), O_RDONLY);
    if (sofd == -1) {
        return find_so_func_addr_by_mem(pid, so, funcname, funcaddr_plt, funcaddr);
    } else {
        return find_so_func_addr_by_file(pid, so, funcname, funcaddr_plt, funcaddr, sofd);
    }
}

bool ends_with(const std::string &str, const std::string &suffix) {
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

bool starts_with(const std::string &str, const std::string &prefix) {
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
}

int find_libc_name(int pid, std::string &name, void *&psostart) {

    char maps_path[PATH_MAX];
    sprintf(maps_path, "/proc/%d/maps", pid);
    FILE *fd = fopen(maps_path, "r");
    if (!fd) {
        ERR("cannot open the memory maps, %s", strerror(errno));
        return -1;
    }

    name = "";
    std::string sobeginstr;

    char buf[PROCMAPS_LINE_MAX_LENGTH];
    while (!feof(fd)) {
        if (fgets(buf, PROCMAPS_LINE_MAX_LENGTH, fd) == NULL) {
            break;
        }

        std::vector<std::string> tmp;

        const char *sep = "\t \r\n";
        char *line = NULL;
        for (char *token = strtok_r(buf, sep, &line); token != NULL; token = strtok_r(NULL, sep, &line)) {
            tmp.push_back(token);
        }

        if (tmp.empty()) {
            continue;
        }

        std::string path = tmp[tmp.size() - 1];
        if (path == "(deleted)") {
            if (tmp.size() < 2) {
                continue;
            }
            path = tmp[tmp.size() - 2];
        }

        int pos = path.find_last_of("/");
        if (pos == -1) {
            continue;
        }
        std::string targetso = path.substr(pos + 1);
        targetso.erase(std::find_if(targetso.rbegin(), targetso.rend(), [](int ch) {
            return !std::isspace(ch);
        }).base(), targetso.end());

        if (starts_with(targetso, "libc-") && ends_with(targetso, ".so")) {
            name = targetso;
            sobeginstr = tmp[0];
            pos = sobeginstr.find_last_of("-");
            if (pos == -1) {
                ERR("parse /proc/%d/maps fail", pid);
                fclose(fd);
                return -1;
            }
            sobeginstr = sobeginstr.substr(0, pos);
            break;
        }
    }

    fclose(fd);

    if (name.empty()) {
        ERR("not find libc name in /proc/%d/maps", pid);
        return -1;
    }

    uint64_t sobeginvalue = std::strtoul(sobeginstr.c_str(), 0, 16);
    psostart = (void *) sobeginvalue;

    return 0;
}

int get_mem_mapping(int pid, std::vector<std::pair<uint64_t, uint64_t>> &mapping) {

    mapping.clear();

    char maps_path[PATH_MAX];
    sprintf(maps_path, "/proc/%d/maps", pid);
    FILE *fd = fopen(maps_path, "r");
    if (!fd) {
        ERR("cannot open the memory maps, %s", strerror(errno));
        return -1;
    }

    char buf[PROCMAPS_LINE_MAX_LENGTH];
    while (!feof(fd)) {
        if (fgets(buf, PROCMAPS_LINE_MAX_LENGTH, fd) == NULL) {
            break;
        }

        std::vector<std::string> tmp;

        const char *sep = "\t \r\n";
        char *line = NULL;
        for (char *token = strtok_r(buf, sep, &line); token != NULL; token = strtok_r(NULL, sep, &line)) {
            tmp.push_back(token);
        }

        if (tmp.empty()) {
            continue;
        }

        std::string range = tmp[0];
        int pos = range.find_last_of("-");
        if (pos == -1) {
            continue;
        }

        std::string beginstr = range.substr(0, pos);
        std::string endstr = range.substr(pos + 1);

        uint64_t begin = (uint64_t) std::strtoul(beginstr.c_str(), 0, 16);
        uint64_t end = (uint64_t) std::strtoul(endstr.c_str(), 0, 16);
        mapping.push_back(std::make_pair(begin, end));
    }

    fclose(fd);

    return 0;
}

std::string glibcname = "";
char *gpcalladdr = 0;
uint64_t gbackupcode = 0;
char *gpcallstack = 0;
const int callstack_len = 8 * 1024 * 1024;
std::map<uint64_t, int> gallocmem;

const int syscall_sys_mmap = 9;
const int syscall_sys_mprotect = 10;
const int syscall_sys_munmap = 11;

int funccall_so(int pid, uint64_t &retval, void *funcaddr, uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0,
                uint64_t arg4 = 0, uint64_t arg5 = 0, uint64_t arg6 = 0) {

    struct user_regs_struct oldregs;
    int ret = ptrace(PTRACE_GETREGS, pid, 0, &oldregs);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_GETREGS fail", pid);
        return -1;
    }

    char code[8] = {0};
    // ff d0 : callq *%rax
    code[0] = 0xff;
    code[1] = 0xd0;
    // cc    : int3
    code[2] = 0xcc;
    // nop
    memset(&code[3], 0x90, sizeof(code) - 3);

    // setup registers
    struct user_regs_struct regs = oldregs;
    regs.rip = (uint64_t) gpcalladdr;
    regs.rbp = (uint64_t) (gpcallstack + callstack_len - 16);
    // rsp must be aligned to a 16-byte boundary
    regs.rsp = (uint64_t) (gpcallstack + callstack_len - (2 * 16));
    regs.rax = (uint64_t) funcaddr;
    regs.rdi = arg1;
    regs.rsi = arg2;
    regs.rdx = arg3;
    regs.rcx = arg4;
    regs.r8 = arg5;
    regs.r9 = arg6;

    ret = remote_process_write(pid, gpcalladdr, code, sizeof(code));
    if (ret < 0) {
        return -1;
    }

    ret = ptrace(PTRACE_SETREGS, pid, 0, &regs);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_SETREGS fail", pid);
        return -1;
    }

    ret = ptrace(PTRACE_CONT, pid, 0, 0);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_CONT fail", pid);
        return -1;
    }

    int errsv = 0;
    int status = 0;
    while (1) {
        ret = waitpid(pid, &status, 0);
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            ERR("waitpid error: %d %s", errno, strerror(errno));
            errsv = errno;
            break;
        }

        if (WIFSTOPPED(status)) {
            if (WSTOPSIG(status) == SIGTRAP) {
                // ok
                break;
            } else if (WSTOPSIG(status) == SIGALRM || WSTOPSIG(status) == SIGPROF || WSTOPSIG(status) == SIGCHLD) {
                ret = ptrace(PTRACE_CONT, pid, 0, 0);
                if (ret < 0) {
                    ERR("ptrace %d PTRACE_CONT fail", pid);
                    return -1;
                }
                continue;
            } else {
                ERR("the target process unexpectedly stopped by signal %d", WSTOPSIG(status));
                errsv = -1;
                break;
            }
        } else if (WIFEXITED(status)) {
            ERR("the target process unexpectedly terminated with exit code %d", WEXITSTATUS(status));
            errsv = -1;
            break;
        } else if (WIFSIGNALED(status)) {
            ERR("the target process unexpectedly terminated by signal %d", WTERMSIG(status));
            errsv = -1;
            break;
        } else {
            ERR("unexpected waitpid status: 0x%x", status);
            errsv = -1;
            break;
        }
    }

    if (!errsv) {
        int ret = ptrace(PTRACE_GETREGS, pid, 0, &regs);
        if (ret < 0) {
            ERR("ptrace %d PTRACE_GETREGS fail", pid);
            return -1;
        }
        retval = regs.rax;
    } else {
        retval = -1;
    }

    ret = ptrace(PTRACE_SETREGS, pid, 0, &oldregs);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_SETREGS fail", pid);
        return -1;
    }

    ret = remote_process_write(pid, gpcalladdr, &gbackupcode, sizeof(gbackupcode));
    if (ret < 0) {
        return -1;
    }

    return 0;
}

int syscall_so(int pid, uint64_t &retval, uint64_t syscallno, uint64_t arg1 = 0, uint64_t arg2 = 0,
               uint64_t arg3 = 0, uint64_t arg4 = 0, uint64_t arg5 = 0, uint64_t arg6 = 0) {

    struct user_regs_struct oldregs;
    int ret = ptrace(PTRACE_GETREGS, pid, 0, &oldregs);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_GETREGS fail", pid);
        return -1;
    }

    char code[8] = {0};
    // 0f 05 : syscall
    code[0] = 0x0f;
    code[1] = 0x05;
    // cc    : int3
    code[2] = 0xcc;
    // nop
    memset(&code[3], 0x90, sizeof(code) - 3);

    // setup registers
    struct user_regs_struct regs = oldregs;
    regs.rip = (uint64_t) gpcalladdr;
    regs.rax = syscallno;
    regs.rdi = arg1;
    regs.rsi = arg2;
    regs.rdx = arg3;
    regs.r10 = arg4;
    regs.r8 = arg5;
    regs.r9 = arg6;

    ret = remote_process_write(pid, gpcalladdr, code, sizeof(code));
    if (ret < 0) {
        return -1;
    }

    ret = ptrace(PTRACE_SETREGS, pid, 0, &regs);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_SETREGS fail", pid);
        return -1;
    }

    ret = ptrace(PTRACE_CONT, pid, 0, 0);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_CONT fail", pid);
        return -1;
    }

    int errsv = 0;
    int status = 0;
    while (1) {
        ret = waitpid(pid, &status, 0);
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            ERR("waitpid error: %d %s", errno, strerror(errno));
            errsv = errno;
            break;
        }

        if (WIFSTOPPED(status)) {
            if (WSTOPSIG(status) == SIGTRAP) {
                // ok
                break;
            } else if (WSTOPSIG(status) == SIGALRM || WSTOPSIG(status) == SIGPROF || WSTOPSIG(status) == SIGCHLD) {
                ret = ptrace(PTRACE_CONT, pid, 0, 0);
                if (ret < 0) {
                    ERR("ptrace %d PTRACE_CONT fail", pid);
                    return -1;
                }
                continue;
            } else {
                ERR("the target process unexpectedly stopped by signal %d", WSTOPSIG(status));
                errsv = -1;
                break;
            }
        } else if (WIFEXITED(status)) {
            ERR("the target process unexpectedly terminated with exit code %d", WEXITSTATUS(status));
            errsv = -1;
            break;
        } else if (WIFSIGNALED(status)) {
            ERR("the target process unexpectedly terminated by signal %d", WTERMSIG(status));
            errsv = -1;
            break;
        } else {
            ERR("unexpected waitpid status: 0x%x", status);
            errsv = -1;
            break;
        }
    }

    if (!errsv) {
        int ret = ptrace(PTRACE_GETREGS, pid, 0, &regs);
        if (ret < 0) {
            ERR("ptrace %d PTRACE_GETREGS fail", pid);
            return -1;
        }
        retval = regs.rax;
    } else {
        retval = -1;
    }

    ret = ptrace(PTRACE_SETREGS, pid, 0, &oldregs);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_SETREGS fail", pid);
        return -1;
    }

    ret = remote_process_write(pid, gpcalladdr, &gbackupcode, sizeof(gbackupcode));
    if (ret < 0) {
        return -1;
    }

    return 0;
}

#pragma pack(1)
struct GlobalMemHead {
    uint64_t magic;
    char magic_name[8];
    uint64_t curlen;
    uint64_t maxlen;
};
struct GlobalMemBody {
    char name[8];
    uint64_t key;
    uint64_t len;
    uint64_t value[0];
};
struct GlobalMem {
    GlobalMemHead head;
    GlobalMemBody body[1];
};
const uint64_t GLOBAL_MEM_HEAD_MAGIC = 0xdead0dad0dadbeef;
const char GLOBAL_MEM_HEAD_MAGIC_NAME[] = "hookso00";
#pragma pack(1)

int alloc_global_mem(int pid, const std::string &namestr, uint64_t key, int len, void *&targetaddr, int &targetlen) {
    char name[8];
    name[sizeof(name) - 1] = 0;
    strncpy(name, namestr.c_str(), sizeof(name) - 1);

    int pagesize = sysconf(_SC_PAGESIZE);

    if (sizeof(GlobalMemHead) + sizeof(GlobalMemBody) + len > pagesize) {
        ERR("alloc_global_mem fail len %d too big, pagesize is %d", len, pagesize);
        return -1;
    }

    std::vector<std::pair<uint64_t, uint64_t>> mapping;
    int ret = get_mem_mapping(pid, mapping);
    if (ret < 0) {
        return -1;
    }

    for (int i = 0; i < (int) mapping.size(); ++i) {
        if (mapping[i].first >= 0xFFFFFFFF || mapping[i].second >= 0xFFFFFFFF) {
            continue;
        }

        for (uint64_t mapping_start = mapping[i].first; mapping_start < mapping[i].second; mapping_start += pagesize) {

            GlobalMemHead head;
            ret = remote_process_read(pid, (void *) mapping_start, &head, sizeof(head));
            if (ret != 0) {
                return -1;
            }

            if (head.magic != GLOBAL_MEM_HEAD_MAGIC ||
                memcmp(head.magic_name, GLOBAL_MEM_HEAD_MAGIC_NAME, sizeof(head.magic_name)) != 0) {
                LOG("get_mem_mapping diff head %llu %llu", mapping_start, mapping[i].second);
                continue;
            }

            void *p = malloc(pagesize);
            ret = remote_process_read(pid, (void *) mapping_start, p, pagesize);
            if (ret != 0) {
                free(p);
                return -1;
            }

            GlobalMem *gm = (GlobalMem *) p;
            for (int i = sizeof(gm->head); i < (int) gm->head.curlen && i < (int) gm->head.maxlen;) {
                GlobalMemBody *body = (GlobalMemBody *) ((uint64_t) gm + i);
                if (body->key == (uint64_t) key && memcmp(body->name, name, sizeof(body->name)) == 0) {
                    targetaddr = (void *) (mapping_start + i + sizeof(GlobalMemBody));
                    targetlen = body->len - sizeof(GlobalMemBody);
                    LOG("get_mem_mapping find old one %llu %d %p %d", mapping_start, i, targetaddr, targetlen);
                    free(p);
                    return 0;
                }
                i += body->len;
                LOG("get_mem_mapping find old diff %llu %llu %llu", body->key, gm->head.curlen, body->len);
            }

            free(p);
        }
    }

    LOG("get_mem_mapping not find old one, start alloc");

    for (int i = 0; i < (int) mapping.size(); ++i) {
        if (mapping[i].first >= 0xFFFFFFFF || mapping[i].second >= 0xFFFFFFFF) {
            continue;
        }

        for (uint64_t mapping_start = mapping[i].first; mapping_start < mapping[i].second; mapping_start += pagesize) {
            GlobalMemHead head;
            ret = remote_process_read(pid, (void *) mapping_start, &head, sizeof(head));
            if (ret != 0) {
                return -1;
            }

            if (head.magic != GLOBAL_MEM_HEAD_MAGIC ||
                memcmp(head.magic_name, GLOBAL_MEM_HEAD_MAGIC_NAME, sizeof(head.magic_name)) != 0) {
                LOG("get_mem_mapping diff head %llu %llu", mapping_start, mapping[i].second);
                continue;
            }

            void *p = malloc(pagesize);
            ret = remote_process_read(pid, (void *) mapping_start, p, pagesize);
            if (ret != 0) {
                free(p);
                return -1;
            }

            GlobalMem *gm = (GlobalMem *) p;
            if (gm->head.curlen + len + sizeof(GlobalMemBody) > gm->head.maxlen) {
                free(p);
                LOG("get_mem_mapping full head %llu", mapping_start);
                continue;
            }

            GlobalMemBody *body = (GlobalMemBody *) ((uint64_t) gm + gm->head.curlen);
            memcpy(body->name, name, sizeof(body->name));
            body->key = key;
            body->len = len + sizeof(GlobalMemBody);

            ret = remote_process_write(pid, (void *) (mapping_start + gm->head.curlen), body, sizeof(GlobalMemBody));
            if (ret != 0) {
                free(p);
                return -1;
            }

            int targetpos = gm->head.curlen;
            gm->head.curlen += body->len;

            ret = remote_process_write(pid, (void *) (mapping_start), gm, sizeof(GlobalMemHead));
            if (ret != 0) {
                free(p);
                return -1;
            }

            targetaddr = (void *) (mapping_start + targetpos + sizeof(GlobalMemBody));
            targetlen = len;

            LOG("get_mem_mapping find new one %llu %p %d", mapping_start, targetaddr, targetlen);
            free(p);

            return 0;
        }
    }

    LOG("get_mem_mapping not alloc from old, start alloc new page");

    uint64_t find = (uint64_t) 0x00400000;
    for (int i = 0; i < (int) mapping.size(); ++i) {
        if (mapping[i].first >= 0xFFFFFFFF || mapping[i].second >= 0xFFFFFFFF) {
            continue;
        }

        if (find >= mapping[i].first && find < mapping[i].second) {
            find = mapping[i].second;
            continue;
        }

        break;
    }

    if (find == (uint64_t) 0x00400000) {
        ERR("alloc_global_mem fail can no find page");
        return -1;
    }

    LOG("get_mem_mapping alloc new page at %p", (void *) find);

    uint64_t retval = 0;
    ret = syscall_so(pid, retval, syscall_sys_mmap, find, pagesize, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ret != 0) {
        return -1;
    }
    if (retval == (uint64_t) (-1)) {
        return -1;
    }

    LOG("get_mem_mapping alloc new page return %p", (void *) retval);

    uint64_t mapping_start = retval;

    void *p = malloc(pagesize);

    GlobalMem *gm = (GlobalMem *) p;
    gm->head.magic = GLOBAL_MEM_HEAD_MAGIC;
    memcpy(gm->head.magic_name, GLOBAL_MEM_HEAD_MAGIC_NAME, sizeof(gm->head.magic_name));
    gm->head.curlen = sizeof(GlobalMemHead);
    gm->head.maxlen = pagesize;

    GlobalMemBody *body = (GlobalMemBody *) ((uint64_t) gm + sizeof(gm->head));
    memcpy(body->name, name, sizeof(body->name));
    body->key = key;
    body->len = len + sizeof(GlobalMemBody);

    ret = remote_process_write(pid, (void *) (mapping_start + gm->head.curlen), body, sizeof(GlobalMemBody));
    if (ret != 0) {
        free(p);
        return -1;
    }

    int targetpos = gm->head.curlen;
    gm->head.curlen += body->len;

    ret = remote_process_write(pid, (void *) (mapping_start), gm, sizeof(GlobalMemHead));
    if (ret != 0) {
        free(p);
        return -1;
    }

    targetaddr = (void *) (mapping_start + targetpos + sizeof(GlobalMemBody));
    targetlen = len;

    LOG("get_mem_mapping alloc new one %llu %p %d", mapping_start, targetaddr, targetlen);
    free(p);

    return 0;
}

int alloc_so_string_mem(int pid, const std::string &str, void *&targetaddr, int &targetlen) {

    int slen = str.length() + 1;
    int pagesize = sysconf(_SC_PAGESIZE);
    int len = ((slen + pagesize - 1) / pagesize) * pagesize;

    LOG("start syscall sys_mmap %d %d", str.length(), len);

    uint64_t retval = 0;
    int ret = syscall_so(pid, retval, syscall_sys_mmap, 0, len, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ret != 0) {
        return -1;
    }
    if (retval == (uint64_t) (-1)) {
        return -1;
    }

    gallocmem[retval] = len;

    ret = remote_process_write(pid, (void *) retval, (void *) str.c_str(), str.length());
    if (ret != 0) {
        return -1;
    }

    targetaddr = (void *) retval;
    targetlen = len;

    LOG("syscall sys_mmap ok %d %d %p", str.length(), len, targetaddr);

    return 0;
}

int free_so_string_mem(int pid, void *targetaddr, int targetlen, bool erasemap = true) {

    LOG("start syscall sys_munmap %p %d", targetaddr, targetlen);

    uint64_t retval = 0;
    int ret = syscall_so(pid, retval, syscall_sys_munmap, (uint64_t) targetaddr, (uint64_t) targetlen);
    if (ret != 0) {
        return -1;
    }
    if (retval == (uint64_t) (-1)) {
        return -1;
    }

    if (erasemap) {
        gallocmem.erase((uint64_t) targetaddr);
    }

    LOG("syscall sys_munmap ok %p %d", targetaddr, targetlen);

    return 0;
}

int get_callstack_func(int pid, std::vector<uint64_t> &cs) {

    struct user_regs_struct oldregs;
    int ret = ptrace(PTRACE_GETREGS, pid, 0, &oldregs);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_GETREGS fail", pid);
        return -1;
    }

    uint64_t ip = (uint64_t) oldregs.rip;
    uint64_t bp = (uint64_t) oldregs.rbp;

    cs.clear();
    cs.push_back(ip);
    LOG("get_callstack_func %p", (void *) ip);

    while (true) {

        void *caller_bp_pointer = (void *) (bp);
        uint64_t caller_bp = 0;
        ret = remote_process_read(pid, caller_bp_pointer, &caller_bp, sizeof(caller_bp), true);
        if (ret != 0 || caller_bp == 0) {
            break;
        }

        void *caller_func_pos = (void *) (bp + sizeof(uint64_t *));
        uint64_t caller_func = 0;
        ret = remote_process_read(pid, caller_func_pos, &caller_func, sizeof(caller_func), true);
        if (ret != 0 || caller_func == 0) {
            break;
        }

        bp = caller_bp;
        cs.push_back(caller_func);
        LOG("get_callstack_func %p", (void *) caller_func);
    }

    return 0;
}

int check_callstack_func_running(int pid, uint64_t modify_ip, int range, bool &running) {
    std::vector<uint64_t> cs;
    running = false;
    int ret = get_callstack_func(pid, cs);
    if (ret != 0) {
        return -1;
    }
    for (int i = 0; i < (int) cs.size(); ++i) {
        if (cs[i] >= modify_ip && cs[i] <= modify_ip + range) {
            running = true;
            break;
        }
    }
    return 0;
}

int inject_so(int pid, const std::string &sopath, uint64_t &handle) {

    char abspath[PATH_MAX];
    if (realpath(sopath.c_str(), abspath) == NULL) {
        ERR("failed to get the full path of %s : %s", sopath.c_str(), strerror(errno));
        return -1;
    }
    LOG("start inject so %s", abspath);

    std::vector<void *> libc_dlopen_mode_funcaddr_plt;
    void *libc_dlopen_mode_funcaddr = 0;
    int ret = find_so_func_addr(pid, glibcname, "__libc_dlopen_mode", libc_dlopen_mode_funcaddr_plt,
                                libc_dlopen_mode_funcaddr);
    if (ret != 0) {
        return -1;
    }
    LOG("libc %s func __libc_dlopen_mode is %p", glibcname.c_str(), libc_dlopen_mode_funcaddr);

    void *dlopen_straddr = 0;
    int dlopen_strlen = 0;
    ret = alloc_so_string_mem(pid, abspath, dlopen_straddr, dlopen_strlen);
    if (ret != 0) {
        return -1;
    }

    uint64_t retval = 0;
    ret = funccall_so(pid, retval, libc_dlopen_mode_funcaddr, (uint64_t) dlopen_straddr, RTLD_LAZY);
    if (ret != 0) {
        return -1;
    }
    if (retval == (uint64_t) (-1)) {
        return -1;
    }

    ret = free_so_string_mem(pid, dlopen_straddr, dlopen_strlen);
    if (ret != 0) {
        return -1;
    }

    LOG("inject so %s ok handle=%lu", abspath, retval);
    handle = retval;

    if (handle == 0) {
        ERR("dlopen %s fail", sopath.c_str());
        return -1;
    }

    return 0;
}

int usage() {
    printf("\n"
           "hookso: type pid params\n"
           "\n"
           "eg:\n"
           "\n"
           "do syscall: \n"
           "# ./hookso syscall pid syscall-number i=int-param1 s=\"string-param2\" \n"
           "\n"
           "call .so function: \n"
           "# ./hookso call pid target-so target-func i=int-param1 s=\"string-param2\" \n"
           "\n"
           "dlopen .so: \n"
           "# ./hookso dlopen pid target-so-path \n"
           "\n"
           "dlclose .so: \n"
           "# ./hookso dlclose pid handle \n"
           "\n"
           "open .so and call function and close: \n"
           "# ./hookso dlcall pid target-so-path target-func i=int-param1 s=\"string-param2\" \n"
           "\n"
           "replace src.so old-function to target.so new-function: \n"
           "# ./hookso replace pid src-so src-func target-so-path target-func \n"
           "\n"
           "replace target-function-addr to target.so new-function: \n"
           "# ./hookso replacep pid func-addr target-so-path target-func \n"
           "\n"
           "set target.so target-function new value : \n"
           "# ./hookso setfunc pid target-so target-func value \n"
           "\n"
           "set target-function-addr new value : \n"
           "# ./hookso setfuncp pid func-addr value \n"
           "\n"
           "find target.so target-function : \n"
           "# ./hookso find pid target-so target-func \n"
           "\n"
           "get target.so target-function call argument: \n"
           "# ./hookso arg pid target-so target-func arg-index \n"
           "\n"
           "get target-function-addr call argument: \n"
           "# ./hookso argp pid func-addr arg-index \n"
           "\n"
           "before call target.so target-function, do syscall/call/dlcall/dlopen/dlclose with params: \n"
           "# ./hookso trigger pid target-so target-func syscall syscall-number @1 i=int-param2 s=\"string-param3\" \n"
           "# ./hookso trigger pid target-so target-func call trigger-target-so trigger-target-func @1 i=int-param2 s=\"string-param3\" \n"
           "# ./hookso trigger pid target-so target-func dlcall trigger-target-so trigger-target-func @1 i=int-param2 s=\"string-param3\" \n"
           "# ./hookso trigger pid target-so target-func dlopen target-so-path\n"
           "# ./hookso trigger pid target-so target-func dlclose handle\n"
           "\n"
           "before call target-function-addr, do syscall/call/dlcall/dlopen/dlclose with params: \n"
           "# ./hookso triggerp pid func-addr syscall syscall-number @1 i=int-param2 s=\"string-param3\" \n"
           "# ./hookso triggerp pid func-addr call trigger-target-so trigger-target-func @1 i=int-param2 s=\"string-param3\" \n"
           "# ./hookso triggerp pid func-addr dlcall trigger-target-so trigger-target-func @1 i=int-param2 s=\"string-param3\" \n"
           "# ./hookso triggerp pid func-addr dlopen target-so-path\n"
           "# ./hookso triggerp pid func-addr dlclose handle\n"
           "\n"
    );
    return -1;
}

int parse_arg_to_so(int pid, const std::string &arg, uint64_t &retval) {

    // i=1234
    if (arg[0] == 'i') {
        retval = std::stoull(arg.substr(2).c_str());
        return 0;
    }

    // s=a b c d
    if (arg[0] == 's') {
        void *arg_straddr = 0;
        int arg_strlen = 0;
        int ret = alloc_so_string_mem(pid, arg.substr(2), arg_straddr, arg_strlen);
        if (ret != 0) {
            return -1;
        }
        retval = (uint64_t) arg_straddr;
        return 0;
    }

    ERR("parse arg fail %s", arg.c_str());
    return -1;
}

int close_so(int pid, uint64_t handle) {

    LOG("start close so %lu", handle);

    std::vector<void *> libc_dlclose_funcaddr_plt;
    void *libc_dlclose_funcaddr = 0;
    int ret = find_so_func_addr(pid, glibcname, "__libc_dlclose", libc_dlclose_funcaddr_plt,
                                libc_dlclose_funcaddr);
    if (ret != 0) {
        return -1;
    }
    LOG("libc %s func __libc_dlclose is %p", glibcname.c_str(), libc_dlclose_funcaddr);

    uint64_t retval = 0;
    ret = funccall_so(pid, retval, libc_dlclose_funcaddr, handle);
    if (ret != 0) {
        return -1;
    }
    if (retval == (uint64_t) (-1)) {
        return -1;
    }

    LOG("close so %lu ok", handle);

    return 0;
}

int program_dlclose_impl(int pid, uint64_t handle) {
    int ret = close_so(pid, handle);
    if (ret != 0) {
        return -1;
    }

    printf("%lu\n", handle);

    return 0;
}

int program_dlclose(int argc, char **argv) {

    if (argc < 4) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string handlestr = argv[3];

    LOG("pid=%s", pidstr.c_str());
    LOG("target so=%s", handlestr.c_str());

    LOG("start remove so file %s", handlestr.c_str());

    int pid = atoi(pidstr.c_str());
    uint64_t handle = std::stoull(handlestr.c_str());

    return program_dlclose_impl(pid, handle);
}

int program_dlopen_impl(int pid, const std::string &targetso) {
    uint64_t handle = 0;
    int ret = inject_so(pid, targetso, handle);
    if (ret != 0) {
        return -1;
    }

    printf("%lu\n", handle);

    return 0;
}

int program_dlopen(int argc, char **argv) {

    if (argc < 4) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string targetso = argv[3];

    LOG("pid=%s", pidstr.c_str());
    LOG("target so=%s", targetso.c_str());

    LOG("start inject so file %s", targetso.c_str());

    int pid = atoi(pidstr.c_str());

    return program_dlopen_impl(pid, targetso);
}

int program_dlcall_impl(int pid, const std::string &targetso, const std::string &targetfunc, uint64_t arg[]) {

    LOG("start inject so file %s %s", targetso.c_str(), targetfunc.c_str());

    uint64_t handle = 0;
    int ret = inject_so(pid, targetso, handle);
    if (ret != 0) {
        return -1;
    }

    LOG("start parse so file %s %s", targetso.c_str(), targetfunc.c_str());

    std::vector<void *> target_funcaddr_plt;
    void *target_funcaddr = 0;
    ret = find_so_func_addr(pid, targetso.c_str(), targetfunc.c_str(), target_funcaddr_plt, target_funcaddr);
    if (ret != 0) {
        close_so(pid, handle);
        return -1;
    }

    LOG("start call so file %s %s", targetso.c_str(), targetfunc.c_str());

    uint64_t retval = 0;
    ret = funccall_so(pid, retval, target_funcaddr, arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
    if (ret != 0) {
        close_so(pid, handle);
        return -1;
    }
    if (retval == (uint64_t) (-1)) {
        close_so(pid, handle);
        return -1;
    }

    LOG("start close so file %s %s", targetso.c_str(), targetfunc.c_str());

    ret = close_so(pid, handle);
    if (ret != 0) {
        return -1;
    }

    printf("%d\n", retval);

    return 0;
}

int program_dlcall(int argc, char **argv) {

    if (argc < 5) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string targetso = argv[3];
    std::string targetfunc = argv[4];
    LOG("pid=%s", pidstr.c_str());
    LOG("target so=%s", targetso.c_str());
    LOG("target function=%s", targetfunc.c_str());

    int pid = atoi(pidstr.c_str());

    uint64_t arg[6] = {0};
    for (int i = 5; i < argc; ++i) {
        std::string argstr = argv[i];
        int ret = parse_arg_to_so(pid, argstr, arg[i - 5]);
        if (ret != 0) {
            return -1;
        }
        LOG("parse %d arg %lu", i - 4, arg[i - 5]);
    }

    int ret = program_dlcall_impl(pid, targetso, targetfunc, arg);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

int program_call_impl(int pid, const std::string &targetso, const std::string &targetfunc, uint64_t arg[]) {

    LOG("start parse so file %s %s", targetso.c_str(), targetfunc.c_str());

    std::vector<void *> target_funcaddr_plt;
    void *target_funcaddr = 0;
    int ret = find_so_func_addr(pid, targetso.c_str(), targetfunc.c_str(), target_funcaddr_plt, target_funcaddr);
    if (ret != 0) {
        return -1;
    }

    uint64_t retval = 0;
    ret = funccall_so(pid, retval, target_funcaddr, arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
    if (ret != 0) {
        return -1;
    }
    if (retval == (uint64_t) (-1)) {
        return -1;
    }

    printf("%d\n", retval);

    return 0;
}

int program_call(int argc, char **argv) {

    if (argc < 5) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string targetso = argv[3];
    std::string targetfunc = argv[4];
    LOG("pid=%s", pidstr.c_str());
    LOG("target so=%s", targetso.c_str());
    LOG("target function=%s", targetfunc.c_str());

    int pid = atoi(pidstr.c_str());

    uint64_t arg[6] = {0};
    for (int i = 5; i < argc; ++i) {
        std::string argstr = argv[i];
        int ret = parse_arg_to_so(pid, argstr, arg[i - 5]);
        if (ret != 0) {
            return -1;
        }
        LOG("parse %d arg %d", i - 4, arg[i - 5]);
    }

    int ret = program_call_impl(pid, targetso, targetfunc, arg);
    if (ret != 0) {
        return -1;
    }

    return 0;
}

int program_syscall_impl(int pid, int syscallno, uint64_t arg[]) {

    LOG("start syscall %d %p %d", syscallno);

    uint64_t retval = 0;
    int ret = syscall_so(pid, retval, syscallno, arg[0], arg[1], arg[2], arg[3], arg[4], arg[5]);
    if (ret != 0) {
        return -1;
    }
    if (retval == (uint64_t) (-1)) {
        return -1;
    }

    printf("%d\n", retval);

    return 0;
}

int program_syscall(int argc, char **argv) {

    if (argc < 4) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string syscallnostr = argv[3];
    LOG("pid=%s", pidstr.c_str());
    LOG("syscall no=%s", syscallnostr.c_str());

    int pid = atoi(pidstr.c_str());

    uint64_t arg[6] = {0};
    for (int i = 4; i < argc; ++i) {
        std::string argstr = argv[i];
        int ret = parse_arg_to_so(pid, argstr, arg[i - 4]);
        if (ret != 0) {
            return -1;
        }
        LOG("parse %d arg %d", i - 3, arg[i - 4]);
    }

    int syscallno = atoi(syscallnostr.c_str());

    int ret = program_syscall_impl(pid, syscallno, arg);
    if (ret != 0) {
        return -1;
    }

    return 0;
}

int program_find(int argc, char **argv) {

    if (argc < 5) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string targetso = argv[3];
    std::string targetfunc = argv[4];

    LOG("pid=%s", pidstr.c_str());
    LOG("target so=%s", targetso.c_str());
    LOG("target function=%s", targetfunc.c_str());

    int pid = atoi(pidstr.c_str());

    LOG("start parse so file %s %s", targetso.c_str(), targetfunc.c_str());

    std::vector<void *> old_funcaddr_plt;
    void *old_funcaddr = 0;
    int ret = find_so_func_addr(pid, targetso.c_str(), targetfunc.c_str(), old_funcaddr_plt, old_funcaddr);
    if (ret != 0) {
        return -1;
    }

    LOG("old %s %s=%p offset=%d", targetso.c_str(), targetfunc.c_str(), old_funcaddr, old_funcaddr_plt.size());

    printf("%p\t%lu\n", old_funcaddr, (uint64_t) old_funcaddr);

    return 0;
}

int program_setfunc(int argc, char **argv) {

    if (argc < 6) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string targetso = argv[3];
    std::string targetfunc = argv[4];
    std::string valuestr = argv[5];

    LOG("pid=%s", pidstr.c_str());
    LOG("target so=%s", targetso.c_str());
    LOG("target function=%s", targetfunc.c_str());
    LOG("valuestr=%s", valuestr.c_str());

    int pid = atoi(pidstr.c_str());
    uint64_t value = std::stoull(valuestr.c_str());

    LOG("start parse so file %s %s", targetso.c_str(), targetfunc.c_str());

    std::vector<void *> old_funcaddr_plt;
    void *old_funcaddr = 0;
    int ret = find_so_func_addr(pid, targetso.c_str(), targetfunc.c_str(), old_funcaddr_plt, old_funcaddr);
    if (ret != 0) {
        return -1;
    }

    LOG("old %s %s=%p offset=%d", targetso.c_str(), targetfunc.c_str(), old_funcaddr, old_funcaddr_plt.size());

    if (old_funcaddr_plt.size() == 0) {
        // func in .so
        uint64_t backup = 0;
        ret = remote_process_read(pid, old_funcaddr, &backup, sizeof(backup));
        if (ret != 0) {
            return -1;
        }

        ret = remote_process_write(pid, old_funcaddr, &value, sizeof(value));
        if (ret != 0) {
            return -1;
        }

        LOG("set text func %s %s ok from %p to %lu", targetso.c_str(), targetfunc.c_str(), old_funcaddr, value);
        printf("%lu\n", backup);
    } else {
        // func out .so
        for (int i = 0; i < (int) old_funcaddr_plt.size(); ++i) {
            void *new_funcaddr = (void *) value;
            ret = remote_process_write(pid, old_funcaddr_plt[i], &new_funcaddr, sizeof(new_funcaddr));
            if (ret != 0) {
                return -1;
            }
            LOG("set plt func %s %s ok from %p to %p", targetso.c_str(), targetfunc.c_str(), old_funcaddr,
                new_funcaddr);
        }
        printf("%lu\n", (uint64_t) old_funcaddr);
    }

    return 0;
}

int program_setfuncp(int argc, char **argv) {

    if (argc < 5) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string targetaddr = argv[3];
    std::string valuestr = argv[4];

    LOG("pid=%s", pidstr.c_str());
    LOG("target addr=%s", targetaddr.c_str());
    LOG("valuestr=%s", valuestr.c_str());

    int pid = atoi(pidstr.c_str());
    uint64_t funcaddr = std::stoull(targetaddr.c_str());
    uint64_t value = std::stoull(valuestr.c_str());

    void *old_funcaddr = (void *) funcaddr;
    LOG("old %p", old_funcaddr);

    // func in .so
    uint64_t backup = 0;
    int ret = remote_process_read(pid, old_funcaddr, &backup, sizeof(backup));
    if (ret != 0) {
        return -1;
    }

    ret = remote_process_write(pid, old_funcaddr, &value, sizeof(value));
    if (ret != 0) {
        return -1;
    }

    LOG("set text func from %p to %lu", old_funcaddr, value);
    printf("%lu\n", backup);

    return 0;
}

int program_replace(int argc, char **argv) {

    if (argc < 7) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string srcso = argv[3];
    std::string srcfunc = argv[4];
    std::string targetso = argv[5];
    std::string targetfunc = argv[6];

    LOG("pid=%s", pidstr.c_str());
    LOG("src so=%s", srcso.c_str());
    LOG("src function=%s", srcfunc.c_str());
    LOG("target so=%s", targetso.c_str());
    LOG("target function=%s", targetfunc.c_str());

    LOG("start parse so file %s %s", srcso.c_str(), srcfunc.c_str());

    int pid = atoi(pidstr.c_str());

    std::vector<void *> old_funcaddr_plt;
    void *old_funcaddr = 0;
    int ret = find_so_func_addr(pid, srcso.c_str(), srcfunc.c_str(), old_funcaddr_plt, old_funcaddr);
    if (ret != 0) {
        return -1;
    }

    LOG("old %s %s=%p offset=%lu", srcso.c_str(), srcfunc.c_str(), old_funcaddr, old_funcaddr_plt);

    LOG("start inject so file %s", targetso.c_str());

    uint64_t handle = 0;
    ret = inject_so(pid, targetso, handle);
    if (ret != 0) {
        return -1;
    }

    LOG("inject so file %s ok", targetso.c_str());

    LOG("start parse so file %s %s", targetso.c_str(), targetfunc.c_str());

    std::vector<void *> new_funcaddr_plt;
    void *new_funcaddr = 0;
    ret = find_so_func_addr(pid, targetso.c_str(), targetfunc.c_str(), new_funcaddr_plt, new_funcaddr);
    if (ret != 0) {
        close_so(pid, handle);
        return -1;
    }

    LOG("new %s %s=%p offset=%d", targetso.c_str(), targetfunc.c_str(), new_funcaddr, new_funcaddr_plt.size());

    if (old_funcaddr_plt.size() == 0) {
        // func in .so
        uint64_t backup = 0;
        ret = remote_process_read(pid, old_funcaddr, &backup, sizeof(backup));
        if (ret != 0) {
            close_so(pid, handle);
            return -1;
        }

        if ((uint64_t) new_funcaddr - ((uint64_t) old_funcaddr + 5) > (uint64_t) 0xFFFFFFFF) {
            ERR("jmp offset too far from %p to %p", (void *) old_funcaddr, (void *) new_funcaddr);
            close_so(pid, handle);
            return -1;
        }

        int offset = (int) ((uint64_t) new_funcaddr - ((uint64_t) old_funcaddr + 5));

        char code[8] = {0};
        code[0] = 0xe9;
        memcpy(&code[1], &offset, sizeof(offset));

        // check
        struct user_regs_struct oldregs;
        ret = ptrace(PTRACE_GETREGS, pid, 0, &oldregs);
        if (ret < 0) {
            ERR("ptrace %d PTRACE_GETREGS fail", pid);
            close_so(pid, handle);
            return -1;
        }

        LOG("cur rip=%lu", (uint64_t) oldregs.rip);

        bool running = false;
        ret = check_callstack_func_running(pid, (uint64_t) old_funcaddr, sizeof(code), running);
        if (ret < 0) {
            close_so(pid, handle);
            return -1;
        }

        if (running) {
            ERR("%d target func %p %u is running, try again", pid, old_funcaddr, (uint64_t) old_funcaddr);
            close_so(pid, handle);
            return -1;
        }

        ret = remote_process_write(pid, old_funcaddr, code, sizeof(code));
        if (ret != 0) {
            close_so(pid, handle);
            return -1;
        }

        LOG("replace text func ok from %s %s=%p to %s %s=%p", srcso.c_str(), srcfunc.c_str(), old_funcaddr,
            targetso.c_str(), targetfunc.c_str(), new_funcaddr);
        printf("%lu\t%lu\n", handle, backup);
    } else {
        // func out .so
        for (int i = 0; i < (int) old_funcaddr_plt.size(); ++i) {
            ret = remote_process_write(pid, old_funcaddr_plt[i], &new_funcaddr, sizeof(new_funcaddr));
            if (ret != 0) {
                close_so(pid, handle);
                return -1;
            }
        }

        LOG("replace plt func ok from %s %s=%p to %s %s=%p", srcso.c_str(), srcfunc.c_str(), old_funcaddr,
            targetso.c_str(), targetfunc.c_str(), new_funcaddr);
        printf("%lu\t%lu\n", handle, (uint64_t) old_funcaddr);
    }

    return 0;
}

int program_replacep(int argc, char **argv) {

    if (argc < 6) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string srcaddr = argv[3];
    std::string targetso = argv[4];
    std::string targetfunc = argv[5];

    LOG("pid=%s", pidstr.c_str());
    LOG("src function addr=%s", srcaddr.c_str());
    LOG("target so=%s", targetso.c_str());
    LOG("target function=%s", targetfunc.c_str());

    int pid = atoi(pidstr.c_str());

    void *old_funcaddr = (void *) std::stoull(srcaddr.c_str());

    LOG("old %s=%p", srcaddr.c_str(), old_funcaddr);

    LOG("start inject so file %s", targetso.c_str());

    uint64_t handle = 0;
    int ret = inject_so(pid, targetso, handle);
    if (ret != 0) {
        return -1;
    }

    LOG("inject so file %s ok", targetso.c_str());

    LOG("start parse so file %s %s", targetso.c_str(), targetfunc.c_str());

    std::vector<void *> new_funcaddr_plt;
    void *new_funcaddr = 0;
    ret = find_so_func_addr(pid, targetso.c_str(), targetfunc.c_str(), new_funcaddr_plt, new_funcaddr);
    if (ret != 0) {
        close_so(pid, handle);
        return -1;
    }

    LOG("new %s %s=%p offset=%d", targetso.c_str(), targetfunc.c_str(), new_funcaddr, new_funcaddr_plt.size());

    uint64_t backup = 0;
    ret = remote_process_read(pid, old_funcaddr, &backup, sizeof(backup));
    if (ret != 0) {
        close_so(pid, handle);
        return -1;
    }

    // check is got
    uint8_t *backup_code = (uint8_t *) &backup;
    if ((uint64_t) old_funcaddr < (uint64_t) 0xFFFFFFFF && backup_code[0] == (uint8_t) 0xFF &&
        backup_code[1] == (uint8_t) 0x25 && // jmpq   *0x200912(%rip)
        backup_code[6] == (uint8_t) 0x68) { // pushq  $0x2

        uint64_t offset = (int) (backup >> 16);
        LOG("got offset=%llu", offset);
        void *gotaddr = (void *) ((uint64_t) old_funcaddr + offset + 6);
        LOG("gotaddr =%p", gotaddr);

        ret = remote_process_read(pid, gotaddr, &backup, sizeof(backup));
        if (ret != 0) {
            close_so(pid, handle);
            return -1;
        }

        ret = remote_process_write(pid, gotaddr, &new_funcaddr, sizeof(new_funcaddr));
        if (ret != 0) {
            close_so(pid, handle);
            return -1;
        }

        LOG("replace got func ok from %s=%p to %s %s=%p", srcaddr.c_str(), old_funcaddr, targetso.c_str(),
            targetfunc.c_str(), new_funcaddr);
        printf("%lu\t%lu\t%lu\n", handle, (uint64_t) gotaddr, backup);

    } else if ((uint64_t) new_funcaddr - ((uint64_t) old_funcaddr + 5) > (uint64_t) 0xFFFFFFFF) {

        void *far_jmpq_addr_pointer = 0;
        int far_jmpq_addr_pointer_len = 0;
        ret = alloc_global_mem(pid, "replace", (uint64_t) old_funcaddr, sizeof(new_funcaddr), far_jmpq_addr_pointer,
                               far_jmpq_addr_pointer_len);
        if (ret < 0) {
            close_so(pid, handle);
            return -1;
        }

        LOG("far jump addr pointer=%p", far_jmpq_addr_pointer);

        int offset = (int) ((uint64_t) far_jmpq_addr_pointer - (uint64_t) old_funcaddr - 6);
        LOG("far jump offset=%d", offset);

        char code[8] = {0};
        code[0] = 0xff;
        code[1] = 0x25;
        memcpy(&code[2], &offset, sizeof(offset));

        // check
        struct user_regs_struct oldregs;
        ret = ptrace(PTRACE_GETREGS, pid, 0, &oldregs);
        if (ret < 0) {
            ERR("ptrace %d PTRACE_GETREGS fail", pid);
            close_so(pid, handle);
            return -1;
        }

        LOG("cur rip=%lu", (uint64_t) oldregs.rip);

        bool running = false;
        ret = check_callstack_func_running(pid, (uint64_t) old_funcaddr, sizeof(code), running);
        if (ret < 0) {
            close_so(pid, handle);
            return -1;
        }

        if (running) {
            ERR("%d target func %p %u is running, try again", pid, old_funcaddr, (uint64_t) old_funcaddr);
            close_so(pid, handle);
            return -1;
        }

        ret = remote_process_write(pid, far_jmpq_addr_pointer, &new_funcaddr, sizeof(new_funcaddr));
        if (ret != 0) {
            close_so(pid, handle);
            return -1;
        }

        ret = remote_process_write(pid, old_funcaddr, code, sizeof(code));
        if (ret != 0) {
            close_so(pid, handle);
            return -1;
        }

        LOG("replace far text func ok from %s=%p to %s %s=%p", srcaddr.c_str(), old_funcaddr, targetso.c_str(),
            targetfunc.c_str(), new_funcaddr);
        printf("%lu\t%lu\t%lu\n", handle, (uint64_t) old_funcaddr, backup);

    } else {

        int offset = (int) ((uint64_t) new_funcaddr - ((uint64_t) old_funcaddr + 5));

        char code[8] = {0};
        code[0] = 0xe9;
        memcpy(&code[1], &offset, sizeof(offset));

        // check
        struct user_regs_struct oldregs;
        ret = ptrace(PTRACE_GETREGS, pid, 0, &oldregs);
        if (ret < 0) {
            ERR("ptrace %d PTRACE_GETREGS fail", pid);
            close_so(pid, handle);
            return -1;
        }

        LOG("cur rip=%lu", (uint64_t) oldregs.rip);

        bool running = false;
        ret = check_callstack_func_running(pid, (uint64_t) old_funcaddr, sizeof(code), running);
        if (ret < 0) {
            close_so(pid, handle);
            return -1;
        }

        if (running) {
            ERR("%d target func %p %u is running, try again", pid, old_funcaddr, (uint64_t) old_funcaddr);
            close_so(pid, handle);
            return -1;
        }

        ret = remote_process_write(pid, old_funcaddr, code, sizeof(code));
        if (ret != 0) {
            close_so(pid, handle);
            return -1;
        }

        LOG("replace text func ok from %s=%p to %s %s=%p", srcaddr.c_str(), old_funcaddr, targetso.c_str(),
            targetfunc.c_str(), new_funcaddr);
        printf("%lu\t%lu\t%lu\n", handle, (uint64_t) old_funcaddr, backup);
    }

    return 0;
}

int grecoverpid;
void *grecoverfuncaddr;
uint64_t grecovercode;

void backup_function(int sig) {
    if (grecoverpid > 0) {
        remote_process_write(grecoverpid, grecoverfuncaddr, &grecovercode, sizeof(grecovercode));
        grecoverpid = 0;
    }
    exit(0);
}

int wait_funccall_addr(int pid, void *old_funcaddr, uint64_t args[]) {

    LOG("start parse so file %p", old_funcaddr);

    uint64_t backup = 0;
    int ret = remote_process_read(pid, old_funcaddr, &backup, sizeof(backup));
    if (ret != 0) {
        return -1;
    }

    LOG("arg %p backup=%lu", old_funcaddr, backup);

    struct user_regs_struct oldregs;
    ret = ptrace(PTRACE_GETREGS, pid, 0, &oldregs);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_GETREGS fail", pid);
        return -1;
    }

    LOG("cur rip=%lu", (uint64_t) oldregs.rip);

    uint64_t newcode = 0;
    char *code = (char *) &newcode;
    // cc    : int3
    code[0] = 0xcc;
    // nop
    memset(&code[1], 0x90, sizeof(code) - 1);

    bool running = false;
    ret = check_callstack_func_running(pid, (uint64_t) old_funcaddr, sizeof(code), running);
    if (ret < 0) {
        return -1;
    }

    if (running) {
        ERR("%d target func %p %u is running, try again", pid, old_funcaddr, (uint64_t) old_funcaddr);
        return -1;
    }

    ret = remote_process_write(pid, old_funcaddr, &newcode, sizeof(newcode));
    if (ret != 0) {
        return -1;
    }

    grecoverpid = pid;
    grecoverfuncaddr = old_funcaddr;
    grecovercode = backup;
    signal(SIGKILL, backup_function);
    signal(SIGSTOP, backup_function);
    signal(SIGTERM, backup_function);
    signal(SIGHUP, backup_function);
    signal(SIGINT, backup_function);
    signal(SIGQUIT, backup_function);
    signal(SIGUSR1, backup_function);

    LOG("set code=%lu", newcode);

    ret = ptrace(PTRACE_CONT, pid, 0, 0);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_CONT fail", pid);
        return -1;
    }

    int errsv = 0;
    int status = 0;
    while (1) {
        ret = waitpid(pid, &status, 0);
        if (ret == -1) {
            if (errno == EINTR) {
                continue;
            }
            ERR("waitpid error: %d %s", errno, strerror(errno));
            errsv = errno;
            break;
        }

        if (WIFSTOPPED(status)) {
            if (WSTOPSIG(status) == SIGTRAP) {
                // ok
                break;
            } else if (WSTOPSIG(status) == SIGALRM || WSTOPSIG(status) == SIGPROF || WSTOPSIG(status) == SIGCHLD) {
                ret = ptrace(PTRACE_CONT, pid, 0, 0);
                if (ret < 0) {
                    ERR("ptrace %d PTRACE_CONT fail", pid);
                    return -1;
                }
                continue;
            } else {
                ERR("the target process unexpectedly stopped by signal %d", WSTOPSIG(status));
                errsv = -1;
                break;
            }
        } else if (WIFEXITED(status)) {
            ERR("the target process unexpectedly terminated with exit code %d", WEXITSTATUS(status));
            errsv = -1;
            break;
        } else if (WIFSIGNALED(status)) {
            ERR("the target process unexpectedly terminated by signal %d", WTERMSIG(status));
            errsv = -1;
            break;
        } else {
            ERR("unexpected waitpid status: 0x%x", status);
            errsv = -1;
            break;
        }
    }

    struct user_regs_struct regs;
    if (!errsv) {
        ret = ptrace(PTRACE_GETREGS, pid, 0, &regs);
        if (ret < 0) {
            ERR("ptrace %d PTRACE_GETREGS fail", pid);
            errsv = -1;
        }
    }

    ret = remote_process_write(pid, old_funcaddr, &backup, sizeof(backup));
    if (ret != 0) {
        return -1;
    }

    grecoverpid = 0;
    grecoverfuncaddr = 0;
    grecovercode = 0;
    signal(SIGKILL, SIG_DFL);
    signal(SIGSTOP, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGUSR1, SIG_DFL);

    LOG("set back=%lu", backup);

    if (errsv != 0) {
        return -1;
    }

    LOG("get regs rip=%lu", (uint64_t) regs.rip);

    regs.rip--;
    ret = ptrace(PTRACE_SETREGS, pid, 0, &regs);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_SETREGS fail", pid);
        return -1;
    }

    args[0] = regs.rdi;
    args[1] = regs.rsi;
    args[2] = regs.rdx;
    args[3] = regs.r10;
    args[4] = regs.r8;
    args[5] = regs.r9;

    return 0;
}

int wait_funccall_so(int pid, const std::string &targetso, const std::string &targetfunc, uint64_t args[]) {

    LOG("start parse so file %s %s", targetso.c_str(), targetfunc.c_str());

    std::vector<void *> old_funcaddr_plt;
    void *old_funcaddr = 0;
    int ret = find_so_func_addr(pid, targetso.c_str(), targetfunc.c_str(), old_funcaddr_plt, old_funcaddr);
    if (ret != 0) {
        return -1;
    }

    return wait_funccall_addr(pid, old_funcaddr, args);
}

int program_argp(int argc, char **argv) {

    if (argc < 5) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string targetaddr = argv[3];
    std::string argindexstr = argv[4];

    LOG("pid=%s", pidstr.c_str());
    LOG("target targetaddr=%s", targetaddr.c_str());
    LOG("arg index=%s", argindexstr.c_str());

    int pid = atoi(pidstr.c_str());
    int argindex = atoi(argindexstr.c_str());

    void *old_funcaddr = (void *) std::stoull(targetaddr.c_str());

    uint64_t args[6] = {0};
    int ret = wait_funccall_addr(pid, old_funcaddr, args);
    if (ret != 0) {
        return -1;
    }

    if (argindex >= 1 && argindex <= 6) {
        printf("%lu\n", args[argindex - 1]);
    }

    return 0;
}

int program_arg(int argc, char **argv) {

    if (argc < 6) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string targetso = argv[3];
    std::string targetfunc = argv[4];
    std::string argindexstr = argv[5];

    LOG("pid=%s", pidstr.c_str());
    LOG("target so=%s", targetso.c_str());
    LOG("target function=%s", targetfunc.c_str());
    LOG("arg index=%s", argindexstr.c_str());

    int pid = atoi(pidstr.c_str());
    int argindex = atoi(argindexstr.c_str());

    uint64_t args[6] = {0};
    int ret = wait_funccall_so(pid, targetso, targetfunc, args);
    if (ret != 0) {
        return -1;
    }

    if (argindex >= 1 && argindex <= 6) {
        printf("%lu\n", args[argindex - 1]);
    }

    return 0;
}

int
program_trigger_impl(int argc, char **argv, int pid, std::string calltype, int calltypeindex, uint64_t target_args[]) {

    std::string trigger_syscallnostr;

    std::string trigger_targetso;
    std::string trigger_targetfunc;

    uint64_t trigger_targethandle;

    int argstart = 0;

    if (calltype == "syscall") {
        if (argc < calltypeindex + 2) {
            return usage();
        }
        trigger_syscallnostr = argv[calltypeindex + 1];
        argstart = calltypeindex + 2;
    } else if (calltype == "dlcall" || calltype == "call") {
        if (argc < calltypeindex + 3) {
            return usage();
        }
        trigger_targetso = argv[calltypeindex + 1];
        trigger_targetfunc = argv[calltypeindex + 2];
        argstart = calltypeindex + 3;
    } else if (calltype == "dlopen") {
        if (argc < calltypeindex + 2) {
            return usage();
        }
        trigger_targetso = argv[calltypeindex + 1];
        argstart = calltypeindex + 2;
    } else if (calltype == "dlclose") {
        if (argc < calltypeindex + 2) {
            return usage();
        }
        trigger_targethandle = std::stoull(argv[calltypeindex + 1]);
        argstart = calltypeindex + 2;
    } else {
        ERR("calltype %s must be syscall/dlcall/call", calltype.c_str());
        return -1;
    }

    uint64_t arg[6] = {0};
    for (int i = argstart; i < argc; ++i) {
        std::string argstr = argv[i];
        if (argstr[0] == '@') {
            int target_index = std::stoi(argstr.substr(1).c_str());
            if (target_index >= 1 && target_index <= 6) {
                arg[i - argstart] = target_args[target_index - 1];
            } else {
                ERR("parse arg fail %s", argstr.c_str());
                return -1;
            }
        } else {
            int ret = parse_arg_to_so(pid, argstr, arg[i - argstart]);
            if (ret != 0) {
                return -1;
            }
        }
        LOG("parse %d arg %d", i - 7, arg[i - argstart]);
    }

    if (calltype == "syscall") {
        int syscallno = atoi(trigger_syscallnostr.c_str());
        int ret = program_syscall_impl(pid, syscallno, arg);
        if (ret != 0) {
            return -1;
        }
    } else if (calltype == "call") {
        int ret = program_call_impl(pid, trigger_targetso, trigger_targetfunc, arg);
        if (ret != 0) {
            return -1;
        }
    } else if (calltype == "dlcall") {
        int ret = program_call_impl(pid, trigger_targetso, trigger_targetfunc, arg);
        if (ret != 0) {
            return -1;
        }
    } else if (calltype == "dlopen") {
        int ret = program_dlopen_impl(pid, trigger_targetso);
        if (ret != 0) {
            return -1;
        }
    } else if (calltype == "dlclose") {
        int ret = program_dlclose_impl(pid, trigger_targethandle);
        if (ret != 0) {
            return -1;
        }
    }

    return 0;
}

int program_triggerp(int argc, char **argv) {

    if (argc < 5) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string targetaddr = argv[3];
    std::string calltype = argv[4];

    int pid = atoi(pidstr.c_str());

    void *old_funcaddr = (void *) std::stoull(targetaddr.c_str());

    uint64_t target_args[6] = {0};
    int ret = wait_funccall_addr(pid, old_funcaddr, target_args);
    if (ret != 0) {
        return -1;
    }

    return program_trigger_impl(argc, argv, pid, calltype, 4, target_args);
}

int program_trigger(int argc, char **argv) {

    if (argc < 6) {
        return usage();
    }

    std::string pidstr = argv[2];
    std::string targetso = argv[3];
    std::string targetfunc = argv[4];
    std::string calltype = argv[5];

    int pid = atoi(pidstr.c_str());

    uint64_t target_args[6] = {0};
    int ret = wait_funccall_so(pid, targetso, targetfunc, target_args);
    if (ret != 0) {
        return -1;
    }

    return program_trigger_impl(argc, argv, pid, calltype, 5, target_args);
}

int ini_hookso_env(int pid) {

    LOG("start ini hookso env");

    int ret = ptrace(PTRACE_ATTACH, pid, 0, 0);
    if (ret < 0) {
        ERR("ptrace %d PTRACE_ATTACH fail", pid);
        return -1;
    }

    ret = waitpid(pid, NULL, 0);
    if (ret < 0) {
        ERR("ptrace %d waitpid fail", pid);
        return -1;
    }

    std::string libcname;
    void *plibcaddr;
    ret = find_libc_name(pid, libcname, plibcaddr);
    if (ret != 0) {
        return -1;
    }
    uint64_t code = 0;
    ret = remote_process_read(pid, plibcaddr, &code, sizeof(code));
    if (ret != 0) {
        return -1;
    }

    glibcname = libcname;
    gpcalladdr = (char *) ((uint64_t) plibcaddr + 8); // Elf64_Ehdr e_ident[8-16]
    gbackupcode = code;

    uint64_t retval = 0;
    ret = syscall_so(pid, retval, syscall_sys_mmap, 0, callstack_len, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN,
                     -1, 0);
    if (ret != 0) {
        return -1;
    }
    if (retval == (uint64_t) (-1)) {
        return -1;
    }
    gpcallstack = (char *) retval;

    LOG("ini hookso env glibcname=%s gpcalladdr=%p backupcode=%lu stack=%p", glibcname.c_str(), gpcalladdr,
        gbackupcode,
        gpcallstack);

    return 0;
}

int fini_hookso_env(int pid) {

    for (const auto &kv : gallocmem) {
        free_so_string_mem(pid, (void *) kv.first, kv.second, false);
    }
    gallocmem.clear();

    uint64_t retval = 0;
    syscall_so(pid, retval, syscall_sys_munmap, (uint64_t) gpcallstack, (uint64_t) callstack_len);

    ptrace(PTRACE_DETACH, pid, 0, 0);

    LOG("fini hookso env ok");

    return 0;
}

int main(int argc, char **argv) {

    if (argc < 3) {
        return usage();
    }

    std::string type = argv[1];
    std::string pidstr = argv[2];

    int pid = atoi(pidstr.c_str());

    int ret = ini_hookso_env(pid);
    if (ret != 0) {
        return -1;
    }

    if (type == "replace") {
        ret = program_replace(argc, argv);
    } else if (type == "replacep") {
        ret = program_replacep(argc, argv);
    } else if (type == "syscall") {
        ret = program_syscall(argc, argv);
    } else if (type == "call") {
        ret = program_call(argc, argv);
    } else if (type == "dlopen") {
        ret = program_dlopen(argc, argv);
    } else if (type == "dlclose") {
        ret = program_dlclose(argc, argv);
    } else if (type == "dlcall") {
        ret = program_dlcall(argc, argv);
    } else if (type == "setfunc") {
        ret = program_setfunc(argc, argv);
    } else if (type == "setfuncp") {
        ret = program_setfuncp(argc, argv);
    } else if (type == "find") {
        ret = program_find(argc, argv);
    } else if (type == "arg") {
        ret = program_arg(argc, argv);
    } else if (type == "argp") {
        ret = program_argp(argc, argv);
    } else if (type == "trigger") {
        ret = program_trigger(argc, argv);
    } else if (type == "triggerp") {
        ret = program_triggerp(argc, argv);
    } else {
        usage();
        ret = -1;
    }

    if (ret != 0) {
        fini_hookso_env(pid);
    } else {
        ret = fini_hookso_env(pid);
    }

    if (ret != 0) {
        return -1;
    }

    return ret;
}

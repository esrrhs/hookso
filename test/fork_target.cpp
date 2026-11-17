// Owned, isolated regression fixture. Never attach this suite to a real service.
#include <signal.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <pthread.h>
#include <ucontext.h>
struct State {
    volatile uint64_t heartbeat, forks, reaped, badchild, chld, prof, traps;
    volatile uint64_t in_remote_handler, remote_active, gate, release, completed, simd_errors, read_result, hold, host_sp;
};
static State *s;
static void handler(int sig, siginfo_t *, void *context) {
    int saved = errno;
    uintptr_t sp = ((ucontext_t *)context)->uc_mcontext.gregs[REG_RSP];
    if (sp > s->host_sp + 1024*1024 || sp + 1024*1024 < s->host_sp) ++s->in_remote_handler;
    if (sig == SIGPROF) ++s->prof;
    if (sig == SIGTRAP || sig == SIGILL) ++s->traps;
    if (sig == SIGCHLD) {
        ++s->chld;
        int st; pid_t p;
        while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
            ++s->reaped;
            if (!WIFEXITED(st) || WEXITSTATUS(st)) ++s->badchild;
        }
    }
    errno = saved;
}
static uint64_t now() {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec)*1000000000 + t.tv_nsec;
}
extern "C" __attribute__((noinline)) uint64_t gate_point(uint64_t x) { asm volatile("" ::: "memory"); return x; }
extern "C" uint64_t remote_probe(uint64_t ms) {
    s->remote_active = 1;
    asm volatile("pxor %%xmm15, %%xmm15" ::: "xmm15");
    uint64_t end = now() + ms*1000000;
    while (now() < end) asm volatile("" ::: "memory");
    s->remote_active = 0; ++s->completed;
    return 31337;
}
extern "C" uint64_t illegal_instruction() { asm volatile("ud2"); return 55; }
extern "C" uint64_t unexpected_trap() { asm volatile("int3"); ++s->completed; return 99; }
extern "C" uint64_t forever() { for (;;) asm volatile("" ::: "memory"); }
static void *idle(void *) { for (;;) usleep(10000); return NULL; }
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--child")) return 0;
    if (argc != 3) return 2;
    int fd = open(argv[1], O_RDWR);
    s = (State *)mmap(NULL, sizeof(State), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (s == MAP_FAILED) return 3;
    close(fd);
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    s->host_sp = (uintptr_t)&fd;
    struct sigaction sa = {}; sa.sa_sigaction = handler; sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGILL, &sa, NULL); sigaction(SIGCHLD, &sa, NULL); sigaction(SIGPROF, &sa, NULL); sigaction(SIGTRAP, &sa, NULL);
    if (!strcmp(argv[2], "threads")) { pthread_t t; pthread_create(&t, NULL, idle, NULL); }
    if (!strcmp(argv[2], "read")) {
        s->heartbeat = 1;
        char c; ssize_t n = read(0, &c, 1);
        s->read_result = n == 1 ? (unsigned char)c : 999;
        while (true) usleep(10000);
    }
    const uint64_t magic = 0x12345678abcdef01ULL;
    asm volatile("movq %0, %%xmm15" :: "r"(magic));
    uint64_t next = now();
    for (;;) {
        ++s->heartbeat;
        uint64_t x; asm volatile("movq %%xmm15, %0" : "=r"(x));
        if (x != magic) { ++s->simd_errors; asm volatile("movq %0, %%xmm15" :: "r"(magic)); }
        if (!s->release && now() >= next) {
            pid_t child = fork();
            if (child == 0) {
                // At least one outstanding child exits during a remote call.
                usleep(30000);
                while (s->hold) usleep(500);
                if (!strcmp(argv[2], "exec")) { execl(argv[0], argv[0], "--child", (char *)NULL); _exit(88); }
                _exit(gate_point(77) == 77 ? 0 : 89);
            }
            if (child < 0) return 4;
            ++s->forks; next = now() + 5000000;
        }
        if (s->gate) {
            s->gate = 0;
            raise(SIGTRAP); // Must be delivered, NOT mistaken for arg completion.
            gate_point(4242);
        }
        usleep(500);
    }
}

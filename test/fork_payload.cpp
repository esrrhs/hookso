#include <stdint.h>
// Resolve against the owned -rdynamic fixture. Run real dlopen constructors
// while child exits and SIGPROF arrive, without involving a Lua installation.
extern "C" uint64_t remote_probe(uint64_t);
__attribute__((constructor)) static void init() { remote_probe(160); }

/* Force-included (build.sh, EXTRAFLAGS) when building with picolibc instead of
 * newlib: ChibiOS's os/various/syscalls.c and QMK's syscall-fallbacks.c take
 * newlib's struct _reent, which picolibc never defines. A forward declaration
 * is enough; the stubs never dereference it (__errno_r is plain errno). */
#ifndef __ASSEMBLER__
struct _reent;
struct timeval;
#endif

/* segvbt.c (MEASUREMENT ONLY): linked into a test program (mpl ... prog.mlb segvbt.c), so no runtime rebuild.
 * On SIGSEGV or SIGBUS, prints the faulting thread's backtrace to stderr ("segv-bt: " line, then the frames),
 * then dies by the same signal. macOS stops writing crash reports after a few dozen crashes of one program. */
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void segvbtHandler(int sig, siginfo_t *info, void *uctx) {
  (void)uctx;
  char buf[96];
  int n = snprintf(buf, sizeof(buf), "segv-bt: signal %d at %p\n", sig, info->si_addr);
  if (n > 0) { ssize_t w = write(2, buf, (size_t)n); (void)w; }
  void *frames[12];
  int nf = backtrace(frames, 12);
  backtrace_symbols_fd(frames, nf, 2);
  signal(sig, SIG_DFL);
  raise(sig);
}
__attribute__((constructor)) static void segvbtInstall(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = segvbtHandler;
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
}

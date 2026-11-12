#ifndef SIGNAL_H
#define SIGNAL_H

#include <runtime/types.h>
#include <signal.h>

#define SIGHUP 1
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGIOT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGALRM 14
#define SIGSTKFLT 16
#define SIGCLD SIGCHLD
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGPOLL SIGIO
#define SIGIO 29
#define SIGPWR 30
#define SIGSYS 31

#define SIGEVENT SIGPOLL
#define SIGIPC SIGUSR1

#define set_signal(mask, sig) *(mask) |= ((u32)1 << (sig - 1))
#define clear_signal(mask, sig) *(mask) &= ~((u32)1 << (sig - 1))
#define is_signal(mask, sig) ((mask) & ((u32)1 << (sig - 1)))

#endif

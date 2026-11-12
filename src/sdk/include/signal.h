#ifndef KMOS_SIGNAL_H
#define KMOS_SIGNAL_H

#define SIGINT 2
#define SIGKILL 9
#define SIGPIPE 13
#define SIGTERM 15
#define SIGCHLD 17

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SA_RESTART 0x10000000U

struct sigaction {
    void (*sa_handler)(int);
    unsigned int sa_mask;
    unsigned int sa_flags;
};

#endif

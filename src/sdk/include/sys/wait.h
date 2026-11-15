#ifndef KMOS_SYS_WAIT_H
#define KMOS_SYS_WAIT_H

#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8

#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status) (((status) >> 8) & 0xff)
#define WIFCONTINUED(status) ((status) == 0xffff)

#endif

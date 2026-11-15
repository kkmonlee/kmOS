#include "user.h"

int main(void) {
    if (kill(getpid(), SIGSTOP)) return 1;
    return puts("continued in background\n") != 0;
}

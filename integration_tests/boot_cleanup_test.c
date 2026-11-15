#include "user.h"

int main(void) {
    int blocked[2], ready[2];
    if (pipe(blocked) || pipe(ready)) return 1;
    int child = fork();
    if (child < 0) return 2;
    char byte;
    if (child) {
        close(ready[1]);
        return read(ready[0], &byte, 1) != 1;
    }
    int grandchild = fork();
    if (grandchild < 0) return 3;
    if (grandchild) {
        close(ready[1]);
        return waitpid(grandchild, 0, 0) != grandchild;
    }
    if (setpgid(0, 0)) return 4;
    if (write(ready[1], "x", 1) != 1) return 5;
    return read(blocked[0], &byte, 1) != 1;
}

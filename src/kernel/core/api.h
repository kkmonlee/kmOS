#ifndef API_H
#define API_H

void call_open();
void call_close();
void call_lseek();
void call_read();
void call_write();
void call_exit();
void call_execv();
void call_wait();
void call_dup2();
void call_fork();
void call_chdir();
void call_getpid();
void call_getppid();
void call_sbrk();
void call_pipe();
void call_kill();
void call_setpgid();
void call_ioctl();

#endif

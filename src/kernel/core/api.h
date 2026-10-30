#ifndef API_H
#define API_H

void call_open();
void call_close();
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

#endif

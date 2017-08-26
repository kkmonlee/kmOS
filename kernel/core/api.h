//
// Created by aa on 26/08/17.
//

#ifndef API_H
#define API_H

// POSIX
void call_open();
void call_close();
void call_read();
void call_write();
void call_sbrk();
void call_ioctl();
void call_exit();
void call_execv();
void call_symlink();
void call_getdents();
void call_wait();
void call_dup2();
void call_fork();
void call_chdir();
void call_mmap();

#endif //API_H

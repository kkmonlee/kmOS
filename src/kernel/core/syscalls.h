#ifndef SYSCALL_H
#define SYSCALL_H

#include <core/file.h>
#include <runtime/list.h>

#define NB_SYSCALLS 100 // maximum number of system calls

typedef void (*syscall_handler)(void); // function pointer type for syscall handlers

class Syscalls
{
public:
  void init();                          // initialize the syscall table
  void add(u32 num, syscall_handler h); // add a handler to the syscall table
  void call(u32 num);                   // call a syscall handler by number

protected:
  syscall_handler calls[NB_SYSCALLS]; // array of syscall handlers
};

extern Syscalls syscall; // global instance of Syscalls

#endif

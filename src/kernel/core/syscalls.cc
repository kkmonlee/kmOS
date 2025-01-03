#include <os.h>
#include <api.h>
#include <api/kernel/syscall_table.h>

#define sysc(a, h) add(a, (syscall_handler)h)

// init syscall table with default handlers
void Syscalls::init()
{
  for (int i = 0; i < NB_SYSCALLS; i++)
  {
    calls[i] = NULL; // set all handlers to NULL initially
  }

  // add specific syscall handlers
  sysc(SYS_open, &call_open);
  sysc(SYS_close, &call_close);
  sysc(SYS_read, &call_read);
  sysc(SYS_write, &call_write);
  sysc(SYS_sbrk, &call_sbrk);
  sysc(SYS_ioctl, &call_ioctl);
  sysc(SYS_exit, &call_exit);
  sysc(SYS_execve, &call_execv);
  sysc(SYS_symlink, &call_symlink);
  sysc(SYS_getdents, &call_getdents);
  sysc(SYS_wait4, &call_wait);
  sysc(SYS_dup2, &call_dup2);
  sysc(SYS_fork, &call_fork);
  sysc(SYS_chdir, &call_chdir);
  sysc(SYS_mmap, &call_mmap);
}

// add a handler to the syscall table
void Syscalls::add(u32 num, syscall_handler h)
{
  calls[num] = h;
}

// call a handler by its syscall number
void Syscalls::call(u32 num)
{
  if (calls[num] != NULL)
  {
    calls[num](); // execute the handler if it exists
  }
}

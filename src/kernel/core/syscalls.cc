#include <os.h>
#include <api.h>
#include <api/kernel/syscall_table.h>
#include <syscalls.h>
#include <architecture.h>

void Syscalls::init()
{
  for (int i = 0; i < NB_SYSCALLS; i++) calls[i] = NULL;
  add(SYS_write, &call_write);
  add(SYS_exit, &call_exit);
  add(SYS_fork, &call_fork);
  add(SYS_waitpid, &call_wait);
  add(SYS_execve, &call_execv);
  add(SYS_open, &call_open);
  add(SYS_close, &call_close);
  add(SYS_lseek, &call_lseek);
  add(SYS_ftruncate, &call_ftruncate);
  add(SYS_unlink, &call_unlink);
  add(SYS_mkdir, &call_mkdir);
  add(SYS_rmdir, &call_rmdir);
  add(SYS_read, &call_read);
  add(SYS_dup2, &call_dup2);
  add(SYS_chdir, &call_chdir);
  add(SYS_getpid, &call_getpid);
  add(SYS_getppid, &call_getppid);
  add(SYS_sbrk, &call_sbrk);
  add(SYS_pipe, &call_pipe);
  add(SYS_kill, &call_kill);
  add(SYS_sigaction, &call_sigaction);
  add(SYS_sigreturn, &call_sigreturn);
  add(SYS_setpgid, &call_setpgid);
  add(SYS_ioctl, &call_ioctl);
}

void Syscalls::add(u32 num, syscall_handler h)
{
  if (num < NB_SYSCALLS) calls[num] = h;
}

bool Syscalls::call(u32 num)
{
  if (num >= NB_SYSCALLS || !calls[num]) return false;
  calls[num]();
  return true;
}

extern "C" InterruptFrame *isr_syscall(InterruptFrame *frame)
{
  Process *process = arch.pcurrent;
  arch.destroy_all_zombie();
  if ((frame->cs & 3) != 3 || !process || process->getPInfo()->pd != current_directory)
    return frame;
  process->getPInfo()->syscall_frame = (u32)frame;
  if (!syscall.call(frame->eax)) frame->eax = (u32)-38;
  process->getPInfo()->syscall_frame = 0;
  if (process->getState() == ZOMBIE) frame = arch.finish_process(process->getExitStatus());
  else if (process->getState() == SLEEPING) frame = arch.suspend_process(frame);
  return arch.return_to_user(frame);
}

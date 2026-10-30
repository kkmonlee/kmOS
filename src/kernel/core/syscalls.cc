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
  if ((frame->cs & 3) != 3 || !process || process->getPInfo()->pd != current_directory)
    return frame;
  process->getPInfo()->syscall_frame = (u32)frame;
  if (!syscall.call(frame->eax)) frame->eax = (u32)-38;
  process->getPInfo()->syscall_frame = 0;
  if (process->getState() == ZOMBIE) return arch.finish_process(process->getExitStatus());
  return frame;
}

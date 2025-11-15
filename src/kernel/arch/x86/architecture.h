#ifndef ARCH_H
#define ARCH_H

#include <runtime/types.h>
#include <process.h>

struct InterruptFrame
{
  u32 gs;
  u32 fs;
  u32 es;
  u32 ds;
  u32 edi;
  u32 esi;
  u32 ebp;
  u32 esp;
  u32 ebx;
  u32 edx;
  u32 ecx;
  u32 eax;
  u32 eip;
  u32 cs;
  u32 eflags;
  u32 useresp;
  u32 ss;
} __attribute__((packed));

class Architecture
{
public:
  void init();
  void reboot();
  void shutdown();
  char *detect();
  void install_irq(int_handler h);
  void addProcess(Process *p);
  void enable_interrupt();
  void disable_interrupt();
  void configure_scheduler(u32 pit_frequency_hz);
  void handle_timer_interrupt(InterruptFrame *frame);

  int createProc(process_st *info, char *file, int argc, char **argv);
  void setParam(u32 ret, u32 ret1, u32 ret2, u32 ret3, u32 ret4);
  u32 getArg(u32 n);
  void setRet(u32 ret);
  void destroy_process(Process *pp);
  void destroy_all_zombie();
  void change_process_father(Process *p, Process *pere);
  int fork(process_st *info, process_st *father);
  struct page_directory *get_current_page_directory();

  Process *pcurrent;
  Process *plist;

private:
  void enqueue_process(Process *proc, bool make_current);
  void ensure_idle_process();
  void save_process_context(Process *proc, const InterruptFrame *frame);
  void load_process_context(Process *proc, InterruptFrame *frame);
  void remove_process(Process *proc);
  Process *find_previous(Process *target);

  u32 ret_reg[5];
  Process *firstProc;
  Process *idle_process;
  u32 ticks_since_switch;
  u32 ticks_per_quantum;
  u64 total_ticks;
  bool scheduler_ready;
};

extern Architecture arch;

#endif

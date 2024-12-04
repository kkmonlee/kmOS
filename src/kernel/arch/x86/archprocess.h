#ifndef APROC_H
#define APROC_H

#include <runtime/types.h>
#include <runtime/list.h>

extern "C"
{
#define KERNELMODE 0
#define USERMODE 1

  struct process_st
  {
    int pid;

    struct
    {
      u32 eax, ecx, edx, ebx;
      u32 esp, ebp, esi, edi;
      u32 eip, eflags;
      u32 cs : 16, ss : 16, ds : 16, es : 16, fs : 16, gs : 16;
      u32 cr3;
    } regs __attribute__((packed));

    struct page_directory *pd;

    list_head pglist;

    char *b_exec;
    char *e_exec;
    char *b_bss;
    char *e_bss;
    char *b_heap;
    char *e_heap;

    u32 signal;
    void *sigfn[32];
    void *vinfo;
  } __attribute__((packed));
}

#endif // APROC_H
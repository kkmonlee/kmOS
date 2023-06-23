#ifndef __X86__
#define __X86__

#include <runtime/types.h>

#define IDTSIZE                 0xFF
#define GDTSIZE                 0xFF

#define IDTBASE                 0x00000000
#define GDTBASE                 0x00000000

#define INTGATE                 0x8E00
#define TRAPGATE                0xEF00

#define KERN_PDIR               0x00001000
#define KERN_STACK              0x0009FFF0
#define KERN_BASE               0x00100000
#define KERN_PG_HEAP            0x00800000
#define KERN_PG_HEAP_LIM        0x10000000
#define KERN_HEAP               0x10000000
#define KERN_HEAP_LIM           0x40000000

#define USER_OFFSET             0x40000000
#define USER_STACK              0xE0000000

#define KERN_PG_1               0x400000
#define KERN_PG_1_LIM           0x800000

#define VADDR_PD_OFFSET(addr)   ((addr) & 0xFFC00000) >> 22
#define VADDR_PT_OFFSET(addr)   ((addr) & 0x003FF000) >> 12
#define VADDR_PG_OFFSET(addr)   (addr) & 0x00000FFF
#define PAGE(addr)              (addr) >> 12

#define PAGING_FLAG             0x80000000
#define PSE_FLAG                0x00000010

#define PG_PRESENT              0x00000001
#define PG_WRITE                0x00000002
#define PG_USER				    0x00000004
#define PG_4MB				    0x00000080

#define	PAGESIZE 			    4096
#define	RAM_MAXSIZE			    0x100000000
#define	RAM_MAXPAGE			    0x100000

struct gdtdesc {
    u16 lim0_15;
    u16 base0_15;
    u8 base16_23;
    u8 acces;
    u8 lim16_19:4;
    u8 other:4;
    u8 base24_31;
} __attribute__ ((packed));

struct gdtr {
    u16 limit;
    u32 base;
} __attribute__ ((packed));

struct tss {
    u16 previous_task, __previous_task_unused;
    u32 esp0;
    u16 ss0, __ss0_unused;
    u32 esp1;
    u16 ss1, __ss1_unused;
    u32 esp2;
    u16 ss2, __ss2_unused;
    u32 cr3;
    u32 eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u16 es, __es_unused;
    u16 cs, __cs_unused;
    u16 ss, __ss_unused;
    u16 ds, __ds_unused;
    u16 fs, __fs_unused;
    u16 gs, __gs_unused;
    u16 ldt_selector, __ldt_sel_unused;
    u16 debug_flag, io_map;
} __attribute__ ((packed));


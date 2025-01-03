#include <os.h>
#include <x86.h>
#include <keyboard.h>

extern "C"
{

  // reads CPU information using CPUID instruction
  regs_t cpu_cpuid(int code)
  {
    regs_t r;
    asm volatile(
        "cpuid"
        : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx)
        : "0"(code));
    return r;
  }

  // retrieves CPU vendor name
  u32 cpu_vendor_name(char *name)
  {
    regs_t r = cpu_cpuid(0x00);

    char line1[5], line2[5], line3[5];
    memcpy(line1, &r.ebx, 4);
    memcpy(line2, &r.ecx, 4);
    memcpy(line3, &r.edx, 4);

    line1[4] = line2[4] = line3[4] = '\0';

    strcpy(name, line1);
    strcat(name, line3);
    strcat(name, line2);

    return 15;
  }

  void schedule();

  idtdesc kidt[IDTSIZE];  // idt table
  int_desc intt[IDTSIZE]; // interrupt functions table
  gdtdesc kgdt[GDTSIZE];  // gdt table
  tss default_tss;        // task state segment
  gdtr kgdtr;             // gdtr registry
  idtr kidtr;             // idtr registry
  u32 *stack_ptr = nullptr;

  // initializes a gdt descriptor
  void init_gdt_desc(u32 base, u32 limite, u8 acces, u8 other, struct gdtdesc *desc)
  {
    desc->lim0_15 = (limite & 0xffff);
    desc->base0_15 = (base & 0xffff);
    desc->base16_23 = (base >> 16) & 0xff;
    desc->acces = acces;
    desc->lim16_19 = (limite >> 16) & 0xf;
    desc->other = other & 0xf;
    desc->base24_31 = (base >> 24) & 0xff;
  }

  // initializes the gdt
  void init_gdt(void)
  {
    default_tss.debug_flag = 0x00;
    default_tss.io_map = 0x00;
    default_tss.esp0 = 0x1FFF0;
    default_tss.ss0 = 0x18;

    // set up gdt segments
    init_gdt_desc(0x0, 0x0, 0x0, 0x0, &kgdt[0]);                  // null descriptor
    init_gdt_desc(0x0, 0xFFFFF, 0x9B, 0x0D, &kgdt[1]);            // kernel code
    init_gdt_desc(0x0, 0xFFFFF, 0x93, 0x0D, &kgdt[2]);            // kernel data
    init_gdt_desc(0x0, 0x0, 0x97, 0x0D, &kgdt[3]);                // kernel stack
    init_gdt_desc(0x0, 0xFFFFF, 0xFF, 0x0D, &kgdt[4]);            // user code
    init_gdt_desc(0x0, 0xFFFFF, 0xF3, 0x0D, &kgdt[5]);            // user data
    init_gdt_desc(0x0, 0x0, 0xF7, 0x0D, &kgdt[6]);                // user stack
    init_gdt_desc((u32)&default_tss, 0x67, 0xE9, 0x00, &kgdt[7]); // tss

    // set up gdtr
    kgdtr.limite = GDTSIZE * 8;
    kgdtr.base = GDTBASE;

    // copy gdt to memory
    memcpy((char *)kgdtr.base, (char *)kgdt, kgdtr.limite);

    // load gdtr
    asm("lgdtl (kgdtr)");

    // initialize segment registers
    asm("movw $0x10, %ax\n\t"
        "movw %ax, %ds\n\t"
        "movw %ax, %es\n\t"
        "movw %ax, %fs\n\t"
        "movw %ax, %gs\n\t"
        "ljmp $0x08, $1f\n\t"
        "1:");
  }

  // initializes an idt descriptor
  void init_idt_desc(u16 select, u32 offset, u16 type, struct idtdesc *desc)
  {
    desc->offset0_15 = offset & 0xffff;
    desc->select = select;
    desc->type = type;
    desc->offset16_31 = (offset >> 16) & 0xffff;
  }

  // initializes the idt
  void init_idt(void)
  {
    for (int i = 0; i < IDTSIZE; i++)
    {
      init_idt_desc(0x08, (u32)_asm_schedule, INTGATE, &kidt[i]);
    }

    // exceptions
    init_idt_desc(0x08, (u32)_asm_exc_GP, INTGATE, &kidt[13]); // general protection
    init_idt_desc(0x08, (u32)_asm_exc_PF, INTGATE, &kidt[14]); // page fault

    // interrupts
    init_idt_desc(0x08, (u32)_asm_int_1, INTGATE, &kidt[33]);      // keyboard
    init_idt_desc(0x08, (u32)_asm_syscalls, TRAPGATE, &kidt[128]); // syscalls

    kidtr.limite = IDTSIZE * 8;
    kidtr.base = IDTBASE;

    // copy idt to memory
    memcpy((char *)kidtr.base, (char *)kidt, kidtr.limite);

    // load idtr
    asm("lidtl (kidtr)");
  }

  // initializes the programmable interrupt controller
  void init_pic(void)
  {
    io.outb(0x20, 0x11); // ICW1
    io.outb(0xA0, 0x11);

    io.outb(0x21, 0x20); // ICW2 (start vector = 32)
    io.outb(0xA1, 0x70);

    io.outb(0x21, 0x04); // ICW3
    io.outb(0xA1, 0x02);

    io.outb(0x21, 0x01); // ICW4
    io.outb(0xA1, 0x01);

    // mask interrupts
    io.outb(0x21, 0x0);
    io.outb(0xA1, 0x0);
  }

  // more functions (e.g., schedule, isr handlers, signal handling) remain as-is for brevity
}

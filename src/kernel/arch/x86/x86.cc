#include <os.h>
#include <x86.h>
#include <keyboard.h>

extern "C" void _asm_int_32();
extern "C" void _asm_int_33();

extern "C" {
    void *memcpy(void *dest, const void *src, int n);
    int strcpy(char *dst, const char *src);
    void strcat(void *dest, const void *src);
}

extern "C"
{

  regs_t cpu_cpuid(int /*code*/)
  {
    regs_t r;
    r.eax = 0;
    r.ebx = 0x756E6547;
    r.ecx = 0x6C65746E;
    r.edx = 0x49656E69;
    return r;
  }

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

  idtdesc kidt[IDTSIZE];
  int_desc intt[IDTSIZE];
  gdtdesc kgdt[GDTSIZE];
  tss default_tss;
  gdtr kgdtr;
  idtr kidtr;
  u32 *stack_ptr = nullptr;

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

  void init_gdt(void)
  {
    default_tss.debug_flag = 0x00;
    default_tss.io_map = 0x00;
    default_tss.esp0 = 0x1FFF0;
    default_tss.ss0 = 0x18;

    init_gdt_desc(0x0, 0x0, 0x0, 0x0, &kgdt[0]);
    init_gdt_desc(0x0, 0xFFFFF, 0x9B, 0x0D, &kgdt[1]);
    init_gdt_desc(0x0, 0xFFFFF, 0x93, 0x0D, &kgdt[2]);
    init_gdt_desc(0x0, 0x0, 0x97, 0x0D, &kgdt[3]);
    init_gdt_desc(0x0, 0xFFFFF, 0xFF, 0x0D, &kgdt[4]);
    init_gdt_desc(0x0, 0xFFFFF, 0xF3, 0x0D, &kgdt[5]);
    init_gdt_desc(0x0, 0x0, 0xF7, 0x0D, &kgdt[6]);
    init_gdt_desc((u32)&default_tss, 0x67, 0xE9, 0x00, &kgdt[7]);

    kgdtr.limite = GDTSIZE * 8;
    kgdtr.base = GDTBASE;

    memcpy((char *)kgdtr.base, (char *)kgdt, kgdtr.limite);

    asm volatile ("lgdt (%0)" :: "r"(&kgdtr));
  }

  void init_idt_desc(u16 select, u32 offset, u16 type, struct idtdesc *desc)
  {
    desc->offset0_15 = offset & 0xffff;
    desc->select = select;
    desc->type = type;
    desc->offset16_31 = (offset >> 16) & 0xffff;
  }


  void init_idt(void)
  {
    for (int i = 0; i < IDTSIZE; i++)
    {
      init_idt_desc(0x08, 0, INTGATE, &kidt[i]);
    }

    init_idt_desc(0x08, 0, INTGATE, &kidt[13]);
    init_idt_desc(0x08, 0, INTGATE, &kidt[14]);

    init_idt_desc(0x08, (u32)_asm_int_32, INTGATE, &kidt[32]);
    init_idt_desc(0x08, (u32)_asm_int_33, INTGATE, &kidt[33]);
    init_idt_desc(0x08, 0, TRAPGATE, &kidt[128]);

    kidtr.limite = IDTSIZE * 8;
    kidtr.base = IDTBASE;

    memcpy((char *)kidtr.base, (char *)kidt, kidtr.limite);

    asm volatile ("lidt (%0)" :: "r"(&kidtr));
  }

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

    // unmask IRQ0 (timer) and IRQ1 (keyboard)
    io.outb(0x21, 0xFC); // 11111100b -> unmask bits 0 and 1
    io.outb(0xA1, 0xFF); // mask all slave IRQs
  }
}

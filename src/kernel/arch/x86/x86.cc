#include <os.h>
#include <x86.h>
#include <keyboard.h>

extern "C" void _asm_int_32();
extern "C" void _asm_int_33();
extern "C" void _asm_int_128();
extern "C" u32 exception_stubs[IDTSIZE];
extern "C" char boot_stack_top;
extern "C" void load_gdt(const gdtr *);

extern "C" {
    void *memcpy(void *dest, const void *src, int n);
    int strcpy(char *dst, const char *src);
    void strcat(void *dest, const void *src);
}

extern "C"
{

  regs_t cpu_cpuid(u32 code)
  {
    regs_t r = {};
    asm volatile("cpuid" : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx)
                 : "a"(code), "c"(0));
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

    return 12;
  }

  void cpu_brand_name(char *name)
  {
    if (cpu_cpuid(0x80000000).eax < 0x80000004) {
      cpu_vendor_name(name);
      return;
    }
    for (u32 i = 0; i < 3; ++i) {
      regs_t r = cpu_cpuid(0x80000002 + i);
      u32 words[] = {r.eax, r.ebx, r.ecx, r.edx};
      memcpy(name + i * 16, words, 16);
    }
    name[48] = 0;
    u32 first = 0;
    while (name[first] == ' ') ++first;
    u32 length = 0;
    while (name[first + length]) { name[length] = name[first + length]; ++length; }
    while (length && name[length - 1] == ' ') --length;
    name[length] = 0;
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
    default_tss.io_map = sizeof(tss);
    default_tss.esp0 = (u32)&boot_stack_top;
    default_tss.ss0 = 0x10;

    init_gdt_desc(0, 0, 0, 0, &kgdt[0]);
    init_gdt_desc(0, 0xFFFFF, 0x9A, 0x0C, &kgdt[1]);
    init_gdt_desc(0, 0xFFFFF, 0x92, 0x0C, &kgdt[2]);
    init_gdt_desc(0, 0, 0, 0, &kgdt[3]);
    init_gdt_desc(0, 0xFFFFF, 0xFA, 0x0C, &kgdt[4]);
    init_gdt_desc(0, 0xFFFFF, 0xF2, 0x0C, &kgdt[5]);
    init_gdt_desc(0, 0, 0, 0, &kgdt[6]);
    init_gdt_desc((u32)&default_tss, sizeof(tss) - 1, 0x89, 0, &kgdt[7]);

    kgdtr.limite = sizeof(kgdt) - 1;
    kgdtr.base = (u32)kgdt;
    load_gdt(&kgdtr);
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
    for (u32 i = 0; i < IDTSIZE; ++i) {
      init_idt_desc(0x08, exception_stubs[i], INTGATE, &kidt[i]);
    }
    init_idt_desc(0x08, (u32)_asm_int_32, INTGATE, &kidt[32]);
    init_idt_desc(0x08, (u32)_asm_int_33, INTGATE, &kidt[33]);
    init_idt_desc(0x08, (u32)_asm_int_128, 0xEE00, &kidt[128]);
    kidtr.limite = sizeof(kidt) - 1;
    kidtr.base = (u32)kidt;
    asm volatile("lidt (%0)" :: "r"(&kidtr) : "memory");
  }

  void init_pic(void)
  {
    io.outb(0x20, 0x11);
    io.outb(0xA0, 0x11);

    io.outb(0x21, 0x20);
    io.outb(0xA1, 0x28);

    io.outb(0x21, 0x04);
    io.outb(0xA1, 0x02);

    io.outb(0x21, 0x01);
    io.outb(0xA1, 0x01);

    io.outb(0x21, 0xFC);
    io.outb(0xA1, 0xFF);
  }
}

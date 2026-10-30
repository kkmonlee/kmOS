#ifndef ELF_H
#define ELF_H

#include <runtime/types.h>
#include <process.h>


typedef struct
{
  unsigned char e_ident[16];
  u16 e_type;
  u16 e_machine;
  u32 e_version;
  u32 e_entry;
  u32 e_phoff;
  u32 e_shoff;
  u32 e_flags;
  u16 e_ehsize;
  u16 e_phentsize;
  u16 e_phnum;
  u16 e_shentsize;
  u16 e_shnum;
  u16 e_shstrndx;
} Elf32_Ehdr;


typedef struct
{
  u32 p_type;
  u32 p_offset;
  u32 p_vaddr;
  u32 p_paddr;
  u32 p_filesz;
  u32 p_memsz;
  u32 p_flags;
  u32 p_align;
} Elf32_Phdr;


typedef struct
{
  u32 name;
  u32 type;
  u32 flags;
  u32 address;
  u32 offset;
  u32 size;
  u32 link;
  u32 info;
  u32 addralign;
  u32 entsize;
} Elf32_Scdr;

bool is_elf(const void *file, u32 size);
u32 load_elf(const void *file, u32 size, process_st *proc, int argc, const char *const *argv,
             int envc = 0, const char *const *envp = nullptr, int *error = nullptr);

#endif

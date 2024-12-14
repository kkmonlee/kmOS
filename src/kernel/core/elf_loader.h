#ifndef ELF_H
#define ELF_H

#include <runtime/types.h>
#include <process.h>

// elf header structure
typedef struct
{
  unsigned char e_ident[16]; // elf identification
  u16 e_type;                // object file type
  u16 e_machine;             // architecture
  u32 e_version;             // version
  u32 e_entry;               // entry point address
  u32 e_phoff;               // program header offset
  u32 e_shoff;               // section header offset
  u32 e_flags;               // processor-specific flags
  u16 e_ehsize;              // elf header size
  u16 e_phentsize;           // program header entry size
  u16 e_phnum;               // number of entries in program header
  u16 e_shentsize;           // section header entry size
  u16 e_shnum;               // number of entries in section header
  u16 e_shstrndx;            // section name string table index
} Elf32_Ehdr;

// program header structure
typedef struct
{
  u32 p_type;   // segment type
  u32 p_offset; // segment file offset
  u32 p_vaddr;  // virtual address
  u32 p_paddr;  // physical address
  u32 p_filesz; // size in file
  u32 p_memsz;  // size in memory
  u32 p_flags;  // segment flags
  u32 p_align;  // alignment
} Elf32_Phdr;

// section header structure
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

int is_elf(char *file);
u32 load_elf(char *file, process_st *proc);
int execv(char *file, int argc, char **argv);
void execv_module(u32 entry, int argc, char **argv);

#endif // ELF_H

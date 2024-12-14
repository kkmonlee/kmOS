#include <os.h>

// default process name and counter
char *__default_proc_name = "_proc_";
char nb_default = '0';

// checks if the file is in elf format
int is_elf(char *file)
{
  Elf32_Ehdr *hdr = (Elf32_Ehdr *)file;
  return (hdr->e_ident[0] == 0x7f && hdr->e_ident[1] == 'E' &&
          hdr->e_ident[2] == 'L' && hdr->e_ident[3] == 'F')
             ? RETURN_OK
             : ERROR_PARAM;
}

// loads elf into memory and returns the entry point
u32 load_elf(char *file, process_st *proc)
{
  Elf32_Ehdr *hdr = (Elf32_Ehdr *)file;
  Elf32_Phdr *p_entry = (Elf32_Phdr *)(file + hdr->e_phoff);

  if (is_elf(file) == ERROR_PARAM)
  {
    io.print("info: load_elf(): file not in elf format\n");
    return 0;
  }

  for (int i = 0; i < hdr->e_phnum; i++, p_entry++)
  {
    if (p_entry->p_type == PT_LOAD)
    {
      u32 v_begin = p_entry->p_vaddr;
      u32 v_end = p_entry->p_vaddr + p_entry->p_memsz;

      if (v_begin < USER_OFFSET || v_end > USER_STACK)
      {
        io.print("info: load_elf(): invalid memory range\n");
        return 0;
      }

      if (p_entry->p_flags == (PF_X | PF_R))
      {
        proc->b_exec = (char *)v_begin;
        proc->e_exec = (char *)v_end;
      }

      if (p_entry->p_flags == (PF_W | PF_R))
      {
        proc->b_bss = (char *)v_begin;
        proc->e_bss = (char *)v_end;
      }

      memcpy((char *)v_begin, (char *)(file + p_entry->p_offset), p_entry->p_filesz);

      if (p_entry->p_memsz > p_entry->p_filesz)
      {
        memset((char *)(v_begin + p_entry->p_filesz), 0,
               p_entry->p_memsz - p_entry->p_filesz);
      }
    }
  }

  return hdr->e_entry;
}

// creates and executes a new process from an elf file
int execv(char *file, int argc, char **argv)
{
  File *fp = fsm.path(file);
  if (!fp)
    return ERROR_PARAM;

  char *map_elf = (char *)kmalloc(fp->getSize());
  fp->open(NO_FLAG);
  fp->read(0, (u8 *)map_elf, fp->getSize());
  fp->close();

  char *name = (argc <= 0) ? __default_proc_name : argv[0];
  Process *proc = new Process(name);
  proc->create(map_elf, argc, argv);

  kfree(map_elf);
  return (int)proc->getPid();
}

// executes a module at the given entry point
void execv_module(u32 entry, int argc, char **argv)
{
  char *name = (argc <= 0) ? __default_proc_name : argv[0];
  Process *proc = new Process(name);
  proc->create((char *)entry, argc, argv);
}

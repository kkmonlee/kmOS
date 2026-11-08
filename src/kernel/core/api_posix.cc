#include <os.h>
#include <process.h>
#include <architecture.h>
#include <filesystem.h>
#include <fcntl.h>
#include <cow.h>
#include <memory_file.h>
#include <api/kernel/syscall_table.h>

static bool bad_pointer(bool valid) {
  if (valid) return false;
  arch.pcurrent->exit(142, 14);
  return true;
}

void call_open() {
  char path[Filesystem::kMaxPathLength];
  Process *process = arch.pcurrent;
  int result = vmm.user_string(process->getPInfo()->pd, arch.getArg(0), path, sizeof(path));
  if (bad_pointer(result != -14)) return;
  if (result) { arch.setRet(result); return; }
  arch.setRet(process->openFile(path, arch.getArg(1), arch.getArg(2)));
}

void call_lseek() {
  Process *process = arch.pcurrent;
  File *file = process->getFile(arch.getArg(0));
  openfile *description = process->getFileInfo(arch.getArg(0));
  if (!file) { arch.setRet((u32)-9); return; }
  if (file->getType() != TYPE_FILE) { arch.setRet((u32)-29); return; }
  u32 origin = arch.getArg(2);
  if (origin > SEEK_END) { arch.setRet((u32)-22); return; }
  s64 base = origin == SEEK_CUR ? description->ptr : (origin == SEEK_END ? file->getSize() : 0);
  s64 position = base + (s32)arch.getArg(1);
  if (position < 0 || position > 0x7fffffff) { arch.setRet((u32)-22); return; }
  description->ptr = (u32)position;
  arch.setRet(description->ptr);
}

void call_close() { arch.setRet(arch.pcurrent->deleteFile(arch.getArg(0))); }

void call_read() {
  Process *process = arch.pcurrent;
  u32 fd = arch.getArg(0), address = arch.getArg(1), size = arch.getArg(2);
  File *file = process->getFile(fd);
  openfile *description = process->getFileInfo(fd);
  if (!file || description->mode == 1) { arch.setRet((u32)-9); return; }
  if (!size) { arch.setRet(0); return; }
  u8 buffer[PAGESIZE];
  u32 count = size > sizeof(buffer) ? sizeof(buffer) : size;
  page_directory *pd = process->getPInfo()->pd;
  if (bad_pointer(vmm.user_write_range(pd, address, count))) return;
  if (file->getType() == TYPE_FILE && count > 0xffffffffU - description->ptr) {
    arch.setRet((u32)-75);
    return;
  }
  if (file->waitable()) {
    int error;
    if (!vmm.prepare_user_write(pd, address, count, &error)) {
      if (!bad_pointer(error != -14)) arch.setRet(error);
      return;
    }
  }
  int result = file->read(description->ptr, buffer, count);
  if (result == -11 && file->waitable()) { process->waitForFile(file, SYS_read); return; }
  if (result > (int)count) result = -5;
  if (result > 0) {
    int error;
    if (!vmm.copy_to_user(pd, address, buffer, result, &error)) {
      if (!bad_pointer(error != -14)) arch.setRet(error);
      return;
    }
    if (file->getType() == TYPE_FILE) description->ptr += result;
  }
  arch.setRet(result);
}

void call_write() {
  Process *process = arch.pcurrent;
  u32 fd = arch.getArg(0), address = arch.getArg(1), size = arch.getArg(2);
  File *file = process->getFile(fd);
  openfile *description = process->getFileInfo(fd);
  if (!file || description->mode == 0) { arch.setRet((u32)-9); return; }
  if (!size) { arch.setRet(0); return; }
  page_directory *pd = process->getPInfo()->pd;
  if (bad_pointer(vmm.user_range(pd, address, size))) return;
  u8 buffer[PAGESIZE];
  u32 count = size > sizeof(buffer) ? sizeof(buffer) : size;
  int error;
  if (!vmm.copy_from_user(pd, buffer, address, count, &error)) {
    if (!bad_pointer(error != -14)) arch.setRet(error);
    return;
  }
  if (file->getType() == TYPE_FILE && count > 0xffffffffU - description->ptr) {
    arch.setRet((u32)-75);
    return;
  }
  int result = file->write(description->ptr, buffer, count);
  if (result == -11 && file->waitable()) { process->waitForFile(file, SYS_write); return; }
  if (result == -32 && file->getType() == TYPE_PIPE) process->sendSignal(SIGPIPE);
  if (result > (int)count) result = -5;
  if (result > 0 && file->getType() == TYPE_FILE) description->ptr += result;
  arch.setRet(result);
}

void call_exit() { arch.pcurrent->exit(arch.getArg(0)); }
void call_fork() { arch.setRet(arch.pcurrent->fork()); }
void call_dup2() { arch.setRet(arch.pcurrent->duplicateFile(arch.getArg(0), arch.getArg(1))); }
void call_wait() { arch.setRet(arch.pcurrent->wait((int)arch.getArg(0), arch.getArg(1), arch.getArg(2))); }
void call_getpid() { arch.setRet(arch.pcurrent->getPid()); }
void call_getppid() {
  Process *parent = arch.pcurrent->getPParent();
  arch.setRet(parent ? parent->getPid() : 0);
}

void call_kill() { arch.setRet(signal_processes(arch.getArg(0), arch.getArg(1))); }

void call_setpgid() { arch.setRet(arch.pcurrent->setGroup(arch.getArg(0), arch.getArg(1))); }

void call_ioctl() {
  File *file = arch.pcurrent->getFile(arch.getArg(0));
  if (!file) { arch.setRet((u32)-9); return; }
  arch.setRet(set_console_foreground(file, arch.getArg(1), arch.getArg(2)));
}

void call_sbrk() {
  process_st *info = arch.pcurrent->getPInfo();
  u32 previous = (u32)info->e_heap;
  s64 requested = (s64)previous + (s32)arch.getArg(0);
  if (!previous || requested < (u32)info->b_heap || requested > USER_IMAGE_LIMIT) {
    arch.setRet((u32)-22);
    return;
  }

  u32 next = (u32)requested;
  u32 first = (previous + PAGESIZE - 1) & ~(PAGESIZE - 1);
  u32 end = (next + PAGESIZE - 1) & ~(PAGESIZE - 1);
  if (end > first && cow_manager.map_pages(info->pd, first, end, PG_PRESENT | PG_WRITE | PG_USER)) {
    arch.setRet((u32)-12);
    return;
  }
  if (end < first) cow_manager.unmap_pages(info->pd, end, first);
  info->e_heap = (char *)next;
  arch.setRet(previous);
}

void call_chdir() {
  char path[Filesystem::kMaxPathLength];
  Process *process = arch.pcurrent;
  int result = vmm.user_string(process->getPInfo()->pd, arch.getArg(0), path, sizeof(path));
  if (bad_pointer(result != -14)) return;
  if (result) { arch.setRet(result); return; }
  File *file = fsm.path(path);
  arch.setRet(!file ? (u32)-2 : (process->setCurrentDir(file) ? 0 : (u32)-20));
}

struct ExecArguments {
  char path[Filesystem::kMaxPathLength];
  char strings[PAGESIZE];
  const char *argv[33];
  const char *envp[33];
  int argc, envc;
  u32 used;
};

static int copy_vector(u32 address, const char **values, int &count, ExecArguments *args) {
  count = 0;
  if (!address) return 0;
  page_directory *pd = arch.pcurrent->getPInfo()->pd;
  for (u32 i = 0; i <= 32; ++i) {
    u32 pointer;
    int error;
    if (address + i * 4 < address) return -14;
    if (!vmm.copy_from_user(pd, &pointer, address + i * 4, 4, &error)) return error;
    if (!pointer) return 0;
    if (i == 32 || args->used == sizeof(args->strings)) return -7;
    u32 capacity = sizeof(args->strings) - args->used;
    if (capacity > 256) capacity = 256;
    char *string = args->strings + args->used;
    int result = vmm.user_string(pd, pointer, string, capacity);
    if (result) return result == -36 ? -7 : result;
    values[count++] = string;
    do { ++args->used; } while (*string++);
  }
  return -7;
}

void call_execv() {
  ExecArguments *args = (ExecArguments *)kmalloc(sizeof(ExecArguments));
  if (!args) { arch.setRet((u32)-12); return; }
  args->used = 0;
  int result = vmm.user_string(arch.pcurrent->getPInfo()->pd, arch.getArg(0), args->path, sizeof(args->path));
  if (!result) result = copy_vector(arch.getArg(1), args->argv, args->argc, args);
  if (!result) result = copy_vector(arch.getArg(2), args->envp, args->envc, args);
  if (!result) result = arch.pcurrent->exec(args->path, args->argc, args->argv, args->envc, args->envp);
  kfree(args);
  if (bad_pointer(result != -14)) return;
  if (result) arch.setRet(result);
}

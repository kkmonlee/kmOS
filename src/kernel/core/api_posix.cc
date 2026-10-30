#include <os.h>
#include <process.h>
#include <architecture.h>
#include <filesystem.h>

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
  File *file = fsm.path(path);
  if (!file) { arch.setRet((u32)-2); return; }
  if (file->getType() != TYPE_FILE && file->getType() != TYPE_DEVICE) {
    arch.setRet((u32)-21);
    return;
  }
  arch.setRet(process->addFile(file, arch.getArg(1)));
}

void call_close() { arch.setRet(arch.pcurrent->deleteFile(arch.getArg(0))); }

void call_read() {
  Process *process = arch.pcurrent;
  u32 fd = arch.getArg(0), address = arch.getArg(1), size = arch.getArg(2);
  File *file = process->getFile(fd);
  openfile *description = process->getFileInfo(fd);
  if (!file || description->mode == 1) { arch.setRet((u32)-9); return; }
  if (!size) { arch.setRet(0); return; }
  if (bad_pointer(vmm.prepare_user_write(process->getPInfo()->pd, address, size))) return;
  u8 buffer[PAGESIZE];
  u32 count = size > sizeof(buffer) ? sizeof(buffer) : size;
  if (file->getType() != TYPE_DEVICE && count > 0xffffffffU - description->ptr) {
    arch.setRet((u32)-75);
    return;
  }
  int result = file->read(description->ptr, buffer, count);
  if (result > (int)count) result = -5;
  if (result > 0) {
    if (bad_pointer(vmm.copy_to_user(process->getPInfo()->pd, address, buffer, result))) return;
    if (file->getType() != TYPE_DEVICE) description->ptr += result;
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
  if (bad_pointer(vmm.copy_from_user(pd, buffer, address, count))) return;
  if (file->getType() != TYPE_DEVICE && count > 0xffffffffU - description->ptr) {
    arch.setRet((u32)-75);
    return;
  }
  int result = file->write(description->ptr, buffer, count);
  if (result > (int)count) result = -5;
  if (result > 0 && file->getType() != TYPE_DEVICE) description->ptr += result;
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
    if (address + i * 4 < address || !vmm.copy_from_user(pd, &pointer, address + i * 4, 4)) return -14;
    if (!pointer) return 0;
    if (i == 32 || args->used == sizeof(args->strings)) return -7;
    u32 capacity = sizeof(args->strings) - args->used;
    if (capacity > 256) capacity = 256;
    char *string = args->strings + args->used;
    int result = vmm.user_string(pd, pointer, string, capacity);
    if (result) return result == -14 ? -14 : -7;
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

#include <os.h>
#include <process.h>
#include <elf_loader.h>
#include <arch/x86/vmm.h>
#include <runtime/alloc.h>
#include <arch/x86/architecture.h>
#include <filesystem.h>

extern "C" {
  int strlen(const char *s);
  void *memcpy(void *dest, const void *src, int n);
  void *memset(void *s, int c, int n);
  int strcmp(const char *s1, const char *s2);
  int strncpy(char *dst, const char *src, int n);
  u64 get_system_ticks();
}

struct page_directory;
extern "C" {
  int cow_fork_mm(page_directory *child_pd, page_directory *parent_pd);
}

extern Architecture arch;
extern Filesystem fsm;

char *Process::default_tty = const_cast<char*>("/dev/tty");
u32 Process::proc_pid = 0;

openfile::openfile(File *file, u32 flags) : File(nullptr, TYPE_LINK), mode(flags), ptr(0), users(1) {
  file->retain();
  setLink(file);
}

openfile::~openfile() {
  File *file = getLink();
  if (file) {
    file->close();
    setLink(nullptr);
    file->release();
  }
}

/* Destructor */
Process::~Process()
{
  closeFiles();
  if (cdir) cdir->release();
  if (info.pd) {
    vmm.destroy_page_directory(info.pd);
  }
  kfree(info.vinfo);
  
  delete ipc;
  arch.change_process_father(this, pparent);
}

Process::Process(char *n) : File(n, TYPE_PROCESS)
{
  memset(&info, 0, sizeof(info));
  pid = proc_pid++;
  info.pid = pid;
  state = SLEEPING;
  exit_status = exit_signal = 0;
  waiting = collected = false;
  wait_child = -1;
  wait_status = 0;
  pparent = 0;
  pnext = 0;
  cdir = arch.pcurrent ? arch.pcurrent->getCurrentDir() : fsm.getRoot();
  if (cdir) cdir->retain();
  time_slice = 0;
  total_runtime = 0;
  last_scheduled = 0;

  for (int i = 0; i < CONFIG_MAX_FILE; i++)
  {
    openfp[i] = nullptr;
    close_on_exec[i] = false;
  }

  void *memory = kmalloc(sizeof(Buffer));
  ipc = memory ? new (memory) Buffer() : nullptr;
}

u32 Process::open(u32 flag)
{
  return RETURN_OK;
}

u32 Process::close()
{
  return RETURN_OK;
}

u32 Process::read(u32 pos, u8 *buffer, u32 sizee)
{
  u32 ret = RETURN_OK;
  if (!ipc) return ERROR_MEMORY;
  arch.enable_interrupt();
  while (ipc->isEmpty())
    ;
  ret = ipc->get(buffer, sizee);
  arch.disable_interrupt();
  return ret;
}

u32 Process::write(u32 pos, u8 *buffer, u32 sizee)
{
  if (!ipc) return ERROR_MEMORY;
  ipc->add(buffer, sizee);
  return sizee;
}

u32 Process::ioctl(u32 id, u8 *buffer)
{
  u32 ret = 0;
  switch (id)
  {
  case API_PROC_GET_PID:
    ret = pid;
    break;
  case API_PROC_GET_INFO:
    reset_pinfo();
    memcpy((char *)buffer, (char *)&ppinfo, sizeof(proc_info));
    ret = 0;
    break;
  default:
    ret = NOT_DEFINED;
    break;
  }
  return ret;
}

u32 Process::remove()
{
  exit();
  if (this != arch.pcurrent) arch.destroy_process(this);
  return RETURN_OK;
}

/* Process-specific methods */
Process *Process::getPParent()
{
  return pparent;
}

void Process::setPParent(Process *p)
{
  pparent = p;
}

bool Process::waitingFor(Process *child) {
  return waiting && child->pparent == this && (wait_child == -1 || wait_child == (int)child->pid);
}

bool Process::completeWait(Process *child) {
  if (!waitingFor(child)) return false;
  u32 status = child->getWaitStatus();
  if (wait_status && !vmm.copy_to_user(info.pd, wait_status, &status, sizeof(status))) {
    exit(142, 14);
    return false;
  }
  info.regs.eax = child->pid;
  ((InterruptFrame *)info.interrupt_stack)->eax = child->pid;
  waiting = false;
  state = READY;
  child->collected = true;
  return true;
}

int Process::wait(int child, u32 status, u32 options) {
  if ((child != -1 && child <= 0) || (options & ~1U)) return -22;
  bool has_child = false;
  Process *cursor = arch.plist;
  if (cursor) do {
    if (cursor->pparent == this && !cursor->collected && (child == -1 || (int)cursor->pid == child)) {
      has_child = true;
      if (cursor->state == ZOMBIE) {
        u32 result = cursor->pid;
        u32 value = cursor->getWaitStatus();
        if (status && !vmm.copy_to_user(info.pd, status, &value, sizeof(value))) {
          exit(142, 14);
          return -14;
        }
        cursor->collected = true;
        arch.destroy_process(cursor);
        return result;
      }
    }
    cursor = cursor->pnext;
  } while (cursor && cursor != arch.plist);
  if (!has_child) return -10;
  if (options & 1) return 0;
  if (status && !vmm.prepare_user_write(info.pd, status, sizeof(u32))) {
    exit(142, 14);
    return -14;
  }
  waiting = true;
  wait_child = child;
  wait_status = status;
  state = SLEEPING;
  return 0;
}

int Process::fork()
{
  if (this != arch.pcurrent || !info.syscall_frame || !info.pd || info.pd != current_directory) return -22;
  if (arch.process_count() >= 64) return -11;
  void *memory = kmalloc(sizeof(Process));
  if (!memory) return -12;
  Process *child = new (memory) Process(getName());
  
  child->pparent = this;
  child->setState(CHILD);
  child->setCurrentDir(getCurrentDir());
  
  for (int i = 0; i < CONFIG_MAX_FILE; i++) {
    child->openfp[i] = openfp[i];
    child->close_on_exec[i] = close_on_exec[i];
    if (openfp[i]) ++openfp[i]->users;
  }
  
  struct page_directory *child_pd = vmm.create_page_directory();
  if (!child_pd) {
    delete child;
    return -12;
  }
  
  if (cow_fork_mm(child_pd, arch.get_current_page_directory()) != 0) {
    vmm.destroy_page_directory(child_pd);
    delete child;
    return -12;
  }
  
  child->info.pd = child_pd;
  
  int ret = arch.fork(&child->info, &info);
  if (ret == 1) {
    child->setState(READY);
    arch.addProcess(child);
    return child->pid;
  } else {
    delete child;
    return -12;
  }
}

u32 Process::create(const void *file, u32 size, int argc, const char *const *argv)
{
  if (!arch.createProc(&info, file, size, argc, argv)) return ERROR_PARAM;
  if (pparent) {
    for (u32 i = 0; i < CONFIG_MAX_FILE; ++i) {
      openfp[i] = pparent->openfp[i];
      close_on_exec[i] = pparent->close_on_exec[i];
      if (openfp[i]) ++openfp[i]->users;
    }
  }
  if (!openfp[0] && !openfp[1] && !openfp[2] && !setupConsole()) return ERROR_MEMORY;
  setState(READY);
  arch.addProcess(this);
  return RETURN_OK;
}

void Process::exit(int status, int signal)
{
  if (state == ZOMBIE) return;
  exit_status = status;
  exit_signal = signal;
  waiting = false;
  closeFiles();
  if (cdir) cdir->release();
  cdir = nullptr;
  setState(ZOMBIE);
  arch.notify_exit(this);
}

int Process::getExitStatus() { return exit_status; }
u32 Process::getWaitStatus() { return exit_signal ? exit_signal & 0x7f : (exit_status & 0xff) << 8; }

int Process::exec(const char *path, int argc, const char *const *argv, int envc, const char *const *envp) {
  File *file = fsm.path(path);
  if (!file) return -2;
  if (file->getType() != TYPE_FILE) return -13;
  u32 size = file->getSize();
  if (!size || size > 16 * 1024 * 1024) return -8;
  void *image = kmalloc(size);
  if (!image) return -12;
  if ((int)file->open(0) < 0) { kfree(image); return -13; }
  u32 count = file->read(0, (u8 *)image, size);
  file->close();
  process_st replacement = {};
  int error = -5;
  bool loaded = count == size && load_elf(image, size, &replacement, argc, argv, envc, envp, &error);
  kfree(image);
  if (!loaded) return error;
  if (!arch.replace_image(&info, &replacement)) {
    vmm.destroy_page_directory(replacement.pd);
    return -22;
  }
  closeFiles(true);
  const char *name = path;
  for (const char *p = path; *p; ++p) if (*p == '/') name = p + 1;
  setName(name);
  return 0;
}

Process *Process::schedule()
{
  if (pnext == NULL) {
    return this;
  }
  
  Process *n = this;
  u32 attempts = 0;
  const u32 max_attempts = 100;
  
  do {
    n = (n->pnext != NULL) ? n->pnext : arch.plist;
    attempts++;
    
    if (attempts > max_attempts) {
      return this;
    }
  } while (n != NULL && (n->state == ZOMBIE || n->state == SLEEPING));
  
  if (n == NULL || n == this) {
    return this;
  }
  
  u64 current_ticks = get_system_ticks();
  
  if (this->state == RUNNING) {
    this->total_runtime += (current_ticks - this->last_scheduled);
    this->state = READY;
  }
  
  n->state = RUNNING;
  n->last_scheduled = current_ticks;
  n->time_slice++;
  
  return n;
}

u64 Process::get_total_runtime()
{
  return total_runtime;
}

u32 Process::get_time_slice_count()
{
  return time_slice;
}

process_st *Process::getPInfo()
{
  return &info;
}

/* File management */
u32 Process::addFile(File *file, u32 mode)
{
  if (!file || (mode & ~(0x80000U | 3U)) || (mode & 3) == 3) return (u32)-22;
  u32 fd = 0;
  while (fd < CONFIG_MAX_FILE && openfp[fd]) ++fd;
  if (fd == CONFIG_MAX_FILE) return (u32)-24;
  int result = (int)file->open(mode & 3);
  if (result < 0) return result;
  void *memory = kmalloc(sizeof(openfile));
  if (!memory) { file->close(); return (u32)-12; }
  openfp[fd] = new (memory) openfile(file, mode & 3);
  close_on_exec[fd] = (mode & 0x80000) != 0;
  return fd;
}

File *Process::getFile(u32 fd)
{
  openfile *description = getFileInfo(fd);
  return description ? description->getLink() : nullptr;
}

openfile *Process::getFileInfo(u32 fd)
{
  return fd < CONFIG_MAX_FILE ? openfp[fd] : nullptr;
}

int Process::deleteFile(u32 fd)
{
  if (fd >= CONFIG_MAX_FILE || !openfp[fd]) return -9;
  openfile *description = openfp[fd];
  openfp[fd] = nullptr;
  close_on_exec[fd] = false;
  if (!--description->users) delete description;
  return 0;
}

int Process::duplicateFile(u32 from, u32 to) {
  if (from >= CONFIG_MAX_FILE || to >= CONFIG_MAX_FILE || !getFile(from)) return -9;
  if (from == to) return to;
  openfile *description = openfp[from];
  ++description->users;
  deleteFile(to);
  openfp[to] = description;
  close_on_exec[to] = false;
  return to;
}

bool Process::setupConsole() {
  File *console = fsm.path(default_tty);
  if (!console) return false;
  return (int)addFile(console, 0) == 0 && (int)addFile(console, 1) == 1 && (int)addFile(console, 1) == 2;
}

void Process::closeFiles(bool exec) {
  for (u32 i = 0; i < CONFIG_MAX_FILE; ++i)
    if (!exec || close_on_exec[i]) deleteFile(i);
}

/* State management */
void Process::setState(u8 st)
{
  state = st;
}

u8 Process::getState()
{
  return state;
}

void Process::setPid(u32 st)
{
  pid = st;
}

u32 Process::getPid()
{
  return pid;
}

void Process::sendSignal(int sig)
{
  set_signal(&(info.signal), sig);
}

void Process::reset_pinfo()
{
  strncpy(ppinfo.name, name, 32);
  ppinfo.pid = pid;
  ppinfo.tid = 0;
  ppinfo.state = state;
  ppinfo.vmem = 10 * 1024 * 1024;
  ppinfo.pmem = 10 * 1024 * 1024;
}

void Process::scan() {
}

File *Process::getCurrentDir()
{
  return cdir ? cdir : fsm.getRoot();
}

bool Process::setCurrentDir(File *f)
{
  if (!f || f->getType() != TYPE_DIRECTORY)
    return false;
  if (f == cdir) return true;
  f->retain();
  if (cdir) cdir->release();
  cdir = f;
  return true;
}

void Process::setPNext(Process *p)
{
  pnext = p;
}

Process *Process::getPNext()
{
  return pnext;
}

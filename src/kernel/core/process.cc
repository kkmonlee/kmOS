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
  void cow_cleanup_process(struct page_directory *pd);
}

extern Architecture arch;
extern Filesystem fsm;

char *Process::default_tty = const_cast<char*>("/dev/tty");
u32 Process::proc_pid = 0;

/* Destructor */
Process::~Process()
{
  if (info.pd) {
    cow_cleanup_process(info.pd);
    vmm.destroy_page_directory(info.pd);
  }
  
  delete ipc;
  arch.change_process_father(this, pparent);
}

Process::Process(char *n) : File(n, TYPE_PROCESS)
{
  pid = proc_pid++;
  state = SLEEPING;
  pparent = 0;
  pnext = 0;
  cdir = 0;
  time_slice = 0;
  total_runtime = 0;
  last_scheduled = 0;

  for (int i = 0; i < CONFIG_MAX_FILE; i++)
  {
    openfp[i].fp = NULL;
    openfp[i].mode = 0;
    openfp[i].ptr = 0;
  }

  ipc = new Buffer();
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
  arch.enable_interrupt();
  while (ipc->isEmpty())
    ;
  ret = ipc->get(buffer, sizee);
  arch.disable_interrupt();
  return ret;
}

u32 Process::write(u32 pos, u8 *buffer, u32 sizee)
{
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
  delete this;
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

u32 Process::wait()
{
  arch.enable_interrupt();
  while (!is_signal(info.signal, SIGCHLD))
    ;
  clear_signal(&(info.signal), SIGCHLD);
  arch.destroy_all_zombie();
  arch.disable_interrupt();
  return 1;
}

int Process::fork()
{
  Process *child = new Process(const_cast<char*>("forked_process"));
  if (!child) {
    return -1;
  }
  
  child->pparent = this;
  child->setState(CHILD);
  child->setCurrentDir(getCurrentDir());
  
  for (int i = 0; i < CONFIG_MAX_FILE; i++) {
    if (openfp[i].fp != NULL) {
      child->openfp[i] = openfp[i];
    }
  }
  
  struct page_directory *child_pd = vmm.create_page_directory();
  if (!child_pd) {
    delete child;
    return -1;
  }
  
  if (cow_fork_mm(child_pd, arch.get_current_page_directory()) != 0) {
    vmm.destroy_page_directory(child_pd);
    delete child;
    return -1;
  }
  
  child->info.pd = child_pd;
  
  int ret = arch.createProc(&child->info, nullptr, 0, nullptr);
  if (ret == 1) {
    child->setState(READY);
    arch.addProcess(child);
    if (pparent != NULL) {
      pparent->sendSignal(SIGCHLD);
    }
    return child->pid;
  } else {
    child->setState(ZOMBIE);
    return 0;
  }
}

u32 Process::create(char *file, int argc, char **argv)
{
  int ret = arch.createProc(&info, file, argc, argv);
  if (ret == 1)
  {
    setState(READY);
    arch.addProcess(this);
  }
  else
  {
    setState(ZOMBIE);
  }

  // Set stdin, stdout, and stderr
  if (pparent != NULL)
  {
    for (int i = 0; i < 3; i++)
    {
      memcpy((char *)&openfp[i], (char *)pparent->getFileInfo(i), sizeof(openfile));
    }
    addFile(this, 0);
  }
  else
  {
    for (int i = 0; i < 3; i++)
    {
      addFile(fsm.path(default_tty), 0);
    }
  }

  return RETURN_OK;
}

void Process::exit()
{
  setState(ZOMBIE);
  if (pparent != NULL)
  {
    pparent->sendSignal(SIGCHLD);
  }
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
u32 Process::addFile(File *f, u32 m)
{
  for (int i = 0; i < CONFIG_MAX_FILE; i++)
  {
    if (openfp[i].fp == NULL && f != NULL)
    {
      openfp[i].fp = f;
      openfp[i].mode = m;
      openfp[i].ptr = 0;
      return i;
    }
  }
  return -1; // Indicate failure to add file
}

File *Process::getFile(u32 fd)
{
  if (fd < 0 || fd >= CONFIG_MAX_FILE)
  {
    return NULL;
  }
  return openfp[fd].fp;
}

openfile *Process::getFileInfo(u32 fd)
{
  if (fd < 0 || fd >= CONFIG_MAX_FILE)
  {
    return NULL;
  }
  return &openfp[fd];
}

void Process::deleteFile(u32 fd)
{
  if (fd < 0 || fd >= CONFIG_MAX_FILE)
  {
    return;
  }
  openfp[fd].fp = NULL;
  openfp[fd].mode = 0;
  openfp[fd].ptr = 0;
}

void Process::setFile(u32 fd, File* file, u32 mode, u32 ptr) {
  if (fd < CONFIG_MAX_FILE) {
    openfp[fd].fp = file;
    openfp[fd].mode = mode;
    openfp[fd].ptr = ptr;
  }
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
  ppinfo.vmem = 10 * 1024 * 1024; // Example values
  ppinfo.pmem = 10 * 1024 * 1024;
}

void Process::scan() {
}

/* Directory management */
File *Process::getCurrentDir()
{
  return cdir;
}

void Process::setCurrentDir(File *f)
{
  cdir = f;
}

void Process::setPNext(Process *p)
{
  pnext = p;
}

Process *Process::getPNext()
{
  return pnext;
}

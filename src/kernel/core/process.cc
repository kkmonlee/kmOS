#include <os.h>
#include <process.h>
#include <architecture.h>
#include <filesystem.h>
#include <archprocess.h>
#include <api/dev/proc.h>
#include <signal.h>
#include <config.h>

extern "C" {
    void *memcpy(void *dest, const void *src, int n);
    char *strncpy(char *destString, const char *sourceString, int maxLength);
}

extern Architecture arch;
extern Filesystem fsm;

char *Process::default_tty = "/dev/tty";
u32 Process::proc_pid = 0;

/* Destructor */
Process::~Process()
{
  delete ipc;
  arch.change_process_father(this, pparent); // Change parent of child processes
}

Process::Process(char *n) : File(n, TYPE_PROCESS)
{
  fsm.addFile("/proc/", this);
  pparent = arch.pcurrent;
  pid = proc_pid++;
  cdir = (pparent != NULL) ? pparent->getCurrentDir() : fsm.getRoot();
  arch.addProcess(this);
  info.vinfo = (void *)this;

  // Initialize open files
  for (int i = 0; i < CONFIG_MAX_FILE; i++)
  {
    openfp[i].fp = NULL;
  }

  ipc = new Buffer(); // IPC buffer
}

/* File operations */
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
  u32 ret;
  switch (id)
  {
  case API_PROC_GET_PID:
    ret = pid;
    break;
  case API_PROC_GET_INFO:
    reset_pinfo();
    memcpy((char *)buffer, (char *)&ppinfo, sizeof(proc_info));
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
  if (pparent != NULL)
  {
    pparent->sendSignal(SIGCHLD);
  }
  return 0;
}

u32 Process::create(char *file, int argc, char **argv)
{
  int ret = arch.createProc(&info, file, argc, argv);
  setState((ret == 1) ? CHILD : ZOMBIE);

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
  Process *n = this;
  do
  {
    n = (n->getPNext() != NULL) ? n->getPNext() : arch.plist;
  } while (n->getState() == ZOMBIE);

  arch.pcurrent = n;
  return n;
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

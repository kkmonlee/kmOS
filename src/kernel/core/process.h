#ifndef PROC_H
#define PROC_H

#include <core/file.h>
#include <runtime/list.h>
#include <archprocess.h>
#include <core/signal.h>
#include <runtime/buffer.h>
#include <api/dev/proc.h>

#define ZOMBIE PROC_STATE_ZOMBIE
#define CHILD PROC_STATE_RUN
#define RUNNING PROC_STATE_RUN
#define SLEEPING 2
#define READY 4

struct openfile : public File
{
  openfile(File *file, u32 flags);
  ~openfile();
  u32 mode;
  u32 ptr;
  u32 users;
};

class Process : public File
{
public:
  friend class Architecture;

  Process(char *n);
  ~Process();

  u32 open(u32 flag) override;
  u32 close() override;
  u32 read(u32 pos, u8 *buffer, u32 size) override;
  u32 write(u32 pos, u8 *buffer, u32 size) override;
  u32 ioctl(u32 id, u8 *buffer) override;
  u32 remove() override;
  void scan() override;

  u32 create(const void *file, u32 size, int argc, const char *const *argv);
  void sendSignal(int sig);
  int wait(int child, u32 status, u32 options);
  bool completeWait(Process *child);
  bool waitingFor(Process *child);
  u32 addFile(File *fp, u32 m);
  File *getFile(u32 fd);
  int deleteFile(u32 fd);
  int duplicateFile(u32 from, u32 to);
  bool setupConsole();
  void closeFiles(bool exec = false);
  openfile *getFileInfo(u32 fd);

  void exit(int status = 0, int signal = 0);
  int getExitStatus();
  u32 getWaitStatus();
  int fork();
  int exec(const char *path, int argc, const char *const *argv, int envc, const char *const *envp);

  void setState(u8 st);
  u8 getState();
  void setPid(u32 pid);
  u32 getPid();

  void setPNext(Process *p);
  Process *schedule();
  Process *getPNext();
  Process *getPParent();
  process_st *getPInfo();
  void setPParent(Process *p);

  void reset_pinfo();

  File *getCurrentDir();
  bool setCurrentDir(File *f);
  
  u64 get_total_runtime();
  u32 get_time_slice_count();

protected:
  static u32 proc_pid;

  u32 pid;
  u8 state;
  int exit_status;
  int exit_signal;
  int wait_child;
  u32 wait_status;
  bool waiting;
  bool collected;
  Process *pparent;
  Process *pnext;
  openfile *openfp[CONFIG_MAX_FILE];
  bool close_on_exec[CONFIG_MAX_FILE];
  proc_info ppinfo;
  process_st info;
  File *cdir;
  Buffer *ipc;
  
  u64 total_runtime;
  u64 last_scheduled;
  u32 time_slice;

  static char *default_tty;
};

#endif

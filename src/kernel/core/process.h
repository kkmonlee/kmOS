#ifndef PROC_H
#define PROC_H

#include <core/file.h>
#include <runtime/list.h>
#include <archprocess.h> // Definition of process_st
#include <core/signal.h>
#include <runtime/buffer.h>
#include <api/dev/proc.h>

// State definitions for the process
#define ZOMBIE PROC_STATE_ZOMBIE
#define CHILD PROC_STATE_RUN

// Structure to represent an open file in a process
struct openfile
{
  u32 mode; // Mode of opening the file
  u32 ptr;  // Read/write pointer
  File *fp; // Pointer to the open file
};

// Process class inherits from File
class Process : public File
{
public:
  Process(char *n); // Constructor
  ~Process();       // Destructor

  // File operations overridden from File
  u32 open(u32 flag) override;
  u32 close() override;
  u32 read(u32 pos, u8 *buffer, u32 size) override;
  u32 write(u32 pos, u8 *buffer, u32 size) override;
  u32 ioctl(u32 id, u8 *buffer) override;
  u32 remove() override;
  void scan() override;

  // Process-specific operations
  u32 create(char *file, int argc, char **argv);
  void sendSignal(int sig);
  u32 wait();
  u32 addFile(File *fp, u32 m);
  File *getFile(u32 fd);
  void deleteFile(u32 fd);
  openfile *getFileInfo(u32 fd);

  void exit();
  int fork();

  // State management
  void setState(u8 st);
  u8 getState();
  void setFile(u32 fd, File *fp, u32 ptr, u32 mode);
  void setPid(u32 pid);
  u32 getPid();

  // Process scheduling
  void setPNext(Process *p);
  Process *schedule();
  Process *getPNext();
  Process *getPParent();
  process_st *getPInfo();
  void setPParent(Process *p);

  void reset_pinfo();

  // Getters/Setters for the current directory
  File *getCurrentDir();
  void setCurrentDir(File *f);

protected:
  static u32 proc_pid; // Static PID tracker

  u32 pid;                          // Process ID
  u8 state;                         // Process state
  Process *pparent;                 // Parent process
  Process *pnext;                   // Next process in the schedule
  openfile openfp[CONFIG_MAX_FILE]; // Array of open files
  proc_info ppinfo;                 // Process information
  File *cdir;                       // Current directory
  Buffer *ipc;                      // Inter-process communication buffer

  static char *default_tty; // Default TTY device
};

#endif // PROC_H

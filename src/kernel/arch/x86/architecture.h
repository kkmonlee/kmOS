#ifndef ARCH_H
#define ARCH_H

#include <runtime/types.h>
#include <process.h>

/**
 * Processor Architecture Class
 * Handles processor-level operations, process scheduling, and system interactions.
 */
class Architecture
{
public:
  /** Architecture class functions **/
  void init();                     // Start the processor interface
  void reboot();                   // Reboot the computer
  void shutdown();                 // Shutdown the computer
  char *detect();                  // Detect the type of processor
  void install_irq(int_handler h); // Install an interrupt handler
  void addProcess(Process *p);     // Add a process to the scheduler
  void enable_interrupt();         // Enable interrupts
  void disable_interrupt();        // Disable interrupts

  /**
   * Process management functions
   */
  int createProc(process_st *info, char *file, int argc, char **argv); // Initialize a process
  void setParam(u32 ret, u32 ret1, u32 ret2, u32 ret3, u32 ret4);      // Set syscall arguments
  u32 getArg(u32 n);                                                   // Get a syscall argument
  void setRet(u32 ret);                                                // Set the syscall return value
  void initProc();                                                     // Initialize the process list
  void destroy_process(Process *pp);                                   // Destroy a process
  void destroy_all_zombie();                                           // Destroy all zombie processes
  void change_process_father(Process *p, Process *pere);               // Change the parent of a process
  int fork(process_st *info, process_st *father);                      // Fork a process
  struct page_directory *get_current_page_directory();                 // Get current page directory

  /** Architecture public attributes **/
  Process *pcurrent; // Pointer to the current process
  Process *plist;    // Pointer to the list of processes

private:
  /** Architecture private attributes **/
  u32 ret_reg[5];     // Registers for return values
  Process *firstProc; // Pointer to the first process
};

/** Standard starting architecture interface **/
extern Architecture arch;

#endif // ARCH_H

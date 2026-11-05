#ifndef MEMORY_FILE_H
#define MEMORY_FILE_H

#include <file.h>

class MemoryFile : public File {
public:
    MemoryFile(const char *name, const void *data, u32 size);
    u32 open(u32 flags) override;
    u32 close() override;
    u32 read(u32 offset, u8 *buffer, u32 count) override;
private:
    const u8 *data;
};

class Process;

void init_console();
void set_console_owner(Process *process);
void poll_console();
int set_console_foreground(File *file, u32 operation, u32 group);

#endif

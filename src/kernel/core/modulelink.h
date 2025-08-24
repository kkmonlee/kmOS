#ifndef MODLINK_H
#define MODLINK_H

#include <core/file.h>
#include <runtime/list.h>

class ModLink : public File
{
public:
  explicit ModLink(const char *name);
  virtual ~ModLink();

  u32 open(u32 flag) override;
  u32 close() override;
  u32 read(u32 pos, u8 *buffer, u32 size) override;
  u32 write(u32 pos, u8 *buffer, u32 size) override;
  u32 ioctl(u32 id, u8 *buffer) override;
  u32 remove() override;
  void scan() override;

protected:
  // additional member variables can be added here if needed
};

#endif // MODLINK_H

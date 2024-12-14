#ifndef ENV_H
#define ENV_H

#include <core/file.h>
#include <runtime/list.h>

class Variable : public File
{
public:
  Variable(const char *name, const char *val);
  virtual ~Variable();

  u32 open(u32 flag) override;
  u32 close() override;
  u32 read(u32 pos, u8 *buffer, u32 size) override;
  u32 write(u32 pos, const u8 *buffer, u32 size) override;
  u32 ioctl(u32 id, u8 *buffer) override;
  u32 remove() override;
  void scan() override;

protected:
  char *value_; // stores the variable's value
};

#endif

#ifndef DEVICE_H
#define DEVICE_H

#include <core/file.h>
#include <runtime/list.h>

class Device : public File
{
public:
  explicit Device(const char *name);
  virtual ~Device();

  // overrides
  virtual u32 open(u32 flag) override;
  virtual u32 close() override;
  virtual u32 read(u32 pos, u8 *buffer, u32 size) override;
  virtual u32 write(u32 pos, u8 *buffer, u32 size) override;
  virtual u32 ioctl(u32 id, u8 *buffer) override;
  virtual u32 remove() override;
  virtual void scan() override;

protected:
  char *name_; // device name
};

#endif // DEVICE_H

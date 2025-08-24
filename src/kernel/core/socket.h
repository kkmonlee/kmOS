#ifndef SOCKET_H
#define SOCKET_H

#include <core/file.h>
#include <runtime/list.h>

class Socket : public File
{
public:
  Socket(const char *name);
  virtual ~Socket();

  u32 open(u32 flag) override;
  u32 close() override;
  u32 read(u32 pos, u8 *buffer, u32 size) override;
  u32 write(u32 pos, u8 *buffer, u32 size) override;
  u32 ioctl(u32 id, u8 *buffer) override;
  u32 remove() override;
  void scan() override;

protected:
  // TODO: maybe socket name? or socket type?
};

#endif // SOCKET_H

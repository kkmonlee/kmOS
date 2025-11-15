#include <os.h>
#include <device.h>
#include <filesystem.h>

extern Filesystem fsm;

Device::~Device()
{
}

Device::Device(const char *n) : File((char*)n, TYPE_DEVICE)
{
  fsm.addFile("/dev", this);
}

u32 Device::open(u32 flag)
{
  (void)flag;
  return RETURN_OK;
}

u32 Device::close()
{
  return RETURN_OK;
}

u32 Device::read(u32 pos, u8 *buffer, u32 size)
{
  (void)pos;
  (void)buffer;
  (void)size;
  return ERROR_PARAM;
}

u32 Device::write(u32 pos, u8 *buffer, u32 size)
{
  (void)pos;
  (void)buffer;
  (void)size;
  return ERROR_PARAM;
}

u32 Device::ioctl(u32 id, u8 *buffer)
{
  (void)id;
  (void)buffer;
  return NOT_DEFINED;
}

u32 Device::remove()
{
  delete this;
  return 0;
}

void Device::scan()
{
}

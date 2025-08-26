#include <os.h>
#include <device.h>
#include <filesystem.h>

extern Filesystem fsm;

Device::~Device()
{
}

Device::Device(const char *n) : File((char*)n, TYPE_DEVICE)
{
  fsm.addFile("/dev", this); // add the device to the filesystem under /dev
}

u32 Device::open(u32 flag)
{
  return NOT_DEFINED; // placeholder for open operation
}

u32 Device::close()
{
  return NOT_DEFINED; // placeholder for close operation
}

u32 Device::read(u32 pos, u8 *buffer, u32 size)
{
  return NOT_DEFINED; // placeholder for read operation
}

u32 Device::write(u32 pos, u8 *buffer, u32 size)
{
  return NOT_DEFINED; // placeholder for write operation
}

u32 Device::ioctl(u32 id, u8 *buffer)
{
  return NOT_DEFINED; // placeholder for ioctl operation
}

u32 Device::remove()
{
  delete this; // delete the device object
  return 0;
}

void Device::scan()
{
  // placeholder for scan operation
}

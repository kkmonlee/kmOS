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
  (void)flag;
  // Base Device class is abstract - subclasses must implement
  // Default behavior: device is always "open" (no exclusive access)
  return RETURN_OK;
}

u32 Device::close()
{
  // Base Device class is abstract - subclasses must implement
  // Default behavior: no cleanup needed for base devices
  return RETURN_OK;
}

u32 Device::read(u32 pos, u8 *buffer, u32 size)
{
  (void)pos;
  (void)buffer;
  (void)size;
  // Base Device must be overridden by subclass for actual I/O
  // Return error if subclass doesn't implement
  return ERROR_PARAM;
}

u32 Device::write(u32 pos, u8 *buffer, u32 size)
{
  (void)pos;
  (void)buffer;
  (void)size;
  // Base Device must be overridden by subclass for actual I/O
  // Return error if subclass doesn't implement
  return ERROR_PARAM;
}

u32 Device::ioctl(u32 id, u8 *buffer)
{
  (void)id;
  (void)buffer;
  // Base Device ioctl - subclasses override for device-specific control
  // Return NOT_DEFINED if the ioctl command isn't recognized
  return NOT_DEFINED;
}

u32 Device::remove()
{
  delete this; // delete the device object
  return 0;
}

void Device::scan()
{
  // Base device scan - enumerate child devices if any
  // Default: no children to scan
  // Subclasses like bus controllers would override this
}

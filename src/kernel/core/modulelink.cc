#include <os.h>

ModLink::ModLink(const char *name) : File(name, TYPE_FILE)
{
  fsm.addFile("/sys/mods/", this);
}

ModLink::~ModLink()
{
  // cleanup code if necessary
}

u32 ModLink::open(u32 flag)
{
  return RETURN_OK;
}

u32 ModLink::close()
{
  return RETURN_OK;
}

u32 ModLink::read(u8 *buffer, u32 size)
{
  return NOT_DEFINED;
}

u32 ModLink::write(const u8 *buffer, u32 size)
{
  return NOT_DEFINED;
}

u32 ModLink::ioctl(u32 id, u8 *buffer)
{
  return NOT_DEFINED;
}

u32 ModLink::remove()
{
  delete this;
  return RETURN_OK;
}

void ModLink::scan()
{
  // implementation if needed
}

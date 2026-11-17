#include <os.h>
#include <modulelink.h>
#include <filesystem.h>

extern Filesystem fsm;

ModLink::ModLink(const char *name) : File((char*)name, TYPE_FILE)
{
  fsm.addFile("/sys/mods/", this);
}

ModLink::~ModLink()
{

}

u32 ModLink::open(u32)
{
  return RETURN_OK;
}

u32 ModLink::close()
{
  return RETURN_OK;
}

u32 ModLink::read(u32, u8 *, u32)
{
  return NOT_DEFINED;
}

u32 ModLink::write(u32, u8 *, u32)
{
  return NOT_DEFINED;
}

u32 ModLink::ioctl(u32, u8 *)
{
  return NOT_DEFINED;
}

u32 ModLink::remove()
{
  return File::remove();
}

void ModLink::scan()
{

}

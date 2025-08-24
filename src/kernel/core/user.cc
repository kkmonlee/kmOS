#include <os.h>
#include <user.h>
#include <filesystem.h>
#include <system.h>

extern "C" {
    void *memset(void *s, int c, int n);
    char *strncpy(char *destString, const char *sourceString, int maxLength);
}

extern Filesystem fsm;
extern System sys;

User::~User() {}

User::User(char *n) : File(n, TYPE_FILE)
{
  fsm.addFile("/sys/usr/", this);
  unext = nullptr;
  sys.addUserToList(this);
  utype = USER_NORM;
  memset(password, 0, sizeof(password));
}

u32 User::open(u32 flag)
{
  return RETURN_OK;
}

u32 User::close()
{
  return RETURN_OK;
}

u32 User::read(u8 *buffer, u32 size)
{
  return NOT_DEFINED;
}

u32 User::write(u8 *buffer, u32 size)
{
  return NOT_DEFINED;
}

u32 User::ioctl(u32 id, u8 *buffer)
{
  return NOT_DEFINED;
}

u32 User::remove()
{
  delete this;
  return RETURN_OK;
}

void User::scan() {}

void User::setPassword(char *n)
{
  if (n == nullptr)
    return;
  memset(password, 0, sizeof(password));
  strncpy(password, n, sizeof(password) - 1); // prevent buffer overflow
}

char *User::getPassword()
{
  if (password[0] == '\0')
    return nullptr;
  return password;
}

User *User::getUNext()
{
  return unext;
}

void User::setUNext(User *us)
{
  unext = us;
}

void User::setUType(u32 t)
{
  utype = t;
}

u32 User::getUType()
{
  return utype;
}

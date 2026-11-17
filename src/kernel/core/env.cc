#include <os.h>
#include <env.h>
#include <filesystem.h>
#include <arch/x86/x86.h>
#include <fcntl.h>

extern "C" {
    int strlen(const char *s);
    void *memcpy(void *dest, const void *src, int n);
    void *memset(void *dest, int value, int n);
}

extern Filesystem fsm;

/*
 * defines the structure of an environment variable
 * each variable is a file stored in the virtual folder /sys/env
 */

Variable::~Variable()
{
  if (value_ != nullptr)
  {
    kfree(value_);
  }
}

Variable::Variable(const char *n, const char *v) : File(n, TYPE_FILE), value_(nullptr)
{
  if (v) setValue(v);
  fsm.addFile("/sys/env/", this);
}

u32 Variable::open(u32 flags)
{
  return flags & O_TRUNC ? truncate(0) : RETURN_OK;
}

u32 Variable::close()
{
  return RETURN_OK;
}

u32 Variable::read(u32 pos, u8 *buffer, u32 count)
{
  IRQGuard guard;
  u32 length = getSize();
  if (!count || pos >= length) return 0;
  if (count > length - pos) count = length - pos;
  memcpy(buffer, value_ + pos, count);
  return count;
}

u32 Variable::write(u32 pos, u8 *buffer, u32 count)
{
  IRQGuard guard;
  if (!count) return 0;
  if (pos > 0x7fffffff || count > 0x7fffffff - pos) return (u32)-27;
  u32 end = pos + count;
  if (end > getSize()) {
    int result = truncate(end);
    if (result) return result;
  }
  memcpy(value_ + pos, buffer, count);
  return count;
}

int Variable::truncate(u32 length)
{
  IRQGuard guard;
  if (length > 0x7fffffff) return -27;
  u32 old_length = getSize();
  if (!length) {
    kfree(value_);
    value_ = nullptr;
  } else {
    if (length > old_length) {
      char *replacement = (char *)krealloc(value_, length + 1);
      if (!replacement) return -12;
      value_ = replacement;
      memset(value_ + old_length, 0, length - old_length);
    }
    value_[length] = '\0';
  }
  setSize(length);
  return 0;
}

int Variable::setValue(const char *value)
{
  IRQGuard guard;
  if (!value) return -22;
  u32 length = strlen(value);
  int result = truncate(length);
  if (!result && length) memcpy(value_, value, length);
  return result;
}

u32 Variable::ioctl(u32 id, u8 *buffer)
{
  if (!buffer) {
    return ERROR_PARAM;
  }

  switch (id) {
    case 0:
      {
        const char* var_name = getName();
        if (var_name) {
          int len = 0;
          while (var_name[len] && len < 255) len++;
          memcpy(buffer, var_name, len + 1);
          return len;
        }
        return 0;
      }

    case 1:
      {
        u32 sz = getSize();
        memcpy(buffer, &sz, sizeof(u32));
        return sizeof(u32);
      }

    case 2:
      {
        u32 readonly = 0;
        memcpy(buffer, &readonly, sizeof(u32));
        return sizeof(u32);
      }

    default:
      return NOT_DEFINED;
  }
}

u32 Variable::remove()
{
  return File::remove();
}

void Variable::scan()
{
}

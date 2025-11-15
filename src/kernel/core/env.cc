#include <os.h>
#include <env.h>
#include <filesystem.h>

extern "C" {
    int strlen(char *s);
    void *memcpy(void *dest, const void *src, int n);
    char *strncpy(char *destString, const char *sourceString, int maxLength);
}

int strnlen(const char *s, int maxlen) {
    int len = 0;
    while (len < maxlen && s[len] != '\0') {
        len++;
    }
    return len;
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

Variable::Variable(const char *n, const char *v) : File((char*)n, TYPE_FILE)
{
  fsm.addFile("/sys/env/", this);

  if (v != nullptr)
  {
    io.print("env: create %s (%s) \n", n, v);
    size_t value_len = strlen((char*)v);
    value_ = (char *)kmalloc(value_len + 1);
    memcpy(value_, v, value_len);
    value_[value_len] = '\0';
    setSize(value_len + 1);
  }
  else
  {
    value_ = nullptr;
  }
}

u32 Variable::open(u32 flag)
{
  return RETURN_OK; // always succeeds
}

u32 Variable::close()
{
  return RETURN_OK; // always succeeds
}

/* reads the value into the buffer */
u32 Variable::read(u32 pos, u8 *buffer, u32 size)
{
  if (value_ == nullptr)
  {
    return NOT_DEFINED;
  }

  size_t read_size = strnlen(value_, size); // safely copy up to `size` bytes
  strncpy((char *)buffer, value_, read_size);
  return read_size;
}

u32 Variable::write(u32 pos, u8 *buffer, u32 size)
{
  if (value_ != nullptr)
  {
    kfree(value_);
  }

  value_ = (char *)kmalloc(size + 1);
  if (value_ != nullptr)
  {
    memcpy(value_, (char *)buffer, size);
    value_[size] = '\0';
    setSize(size + 1);
    return size;
  }

  return NOT_DEFINED;
}

// Variable control via ioctl - supports querying and setting variable properties
u32 Variable::ioctl(u32 id, u8 *buffer)
{
  if (!buffer) {
    return ERROR_PARAM;
  }

  switch (id) {
    case 0: // Get variable name
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

    case 1: // Get variable size
      {
        u32 sz = getSize();
        memcpy(buffer, &sz, sizeof(u32));
        return sizeof(u32);
      }

    case 2: // Check if variable is read-only (future extension)
      {
        u32 readonly = 0; // Currently all variables are writable
        memcpy(buffer, &readonly, sizeof(u32));
        return sizeof(u32);
      }

    default:
      return NOT_DEFINED;
  }
}

u32 Variable::remove()
{
  delete this;
  return RETURN_OK;
}

void Variable::scan()
{
}

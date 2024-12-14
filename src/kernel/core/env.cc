#include <os.h>

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

Variable::Variable(char *n, char *v) : File(n, TYPE_FILE)
{
  fsm.addFile("/sys/env/", this);

  if (v != nullptr)
  {
    io.print("env: create %s (%s) \n", n, v);
    size_t value_len = strlen(v);
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

/* control the variable (TODO) */
u32 Variable::ioctl(u32 id, u8 *buffer)
{
  return NOT_DEFINED; // unimplemented
}

u32 Variable::remove()
{
  delete this;
  return RETURN_OK;
}

void Variable::scan()
{
}

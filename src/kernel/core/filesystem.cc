#include <os.h>
#include <filesystem.h>
#include <filesystem_driver.h>
#include <core/block_device.h>

extern "C" {
    void *memcpy(void *dest, const void *src, int n);
    int strcmp(const char *s1, const char *s2);
    int strcpy(char *dst, const char *src);
  int strlen(const char *s);
}

#include <arch/x86/architecture.h>

Filesystem::Filesystem()
  : driver_count(0), mount_count(0), root(nullptr), dev(nullptr), var(nullptr) {
  for (u32 i = 0; i < kMaxDrivers; ++i) {
    drivers[i] = nullptr;
  }
  for (u32 i = 0; i < kMaxMounts; ++i) {
    mounts[i].path[0] = '\0';
    mounts[i].driver = nullptr;
    mounts[i].device = nullptr;
    mounts[i].mount_point = nullptr;
  }
}

void Filesystem::init()
{
  root = new File("/", TYPE_DIRECTORY);

  dev = root->createChild("dev", TYPE_DIRECTORY);
  root->createChild("proc", TYPE_DIRECTORY);
  root->createChild("mnt", TYPE_DIRECTORY);
  File *sysd = root->createChild("sys", TYPE_DIRECTORY);
  var = sysd->createChild("env", TYPE_DIRECTORY);
  sysd->createChild("usr", TYPE_DIRECTORY);
  sysd->createChild("mods", TYPE_DIRECTORY);
  sysd->createChild("sockets", TYPE_DIRECTORY);
}

Filesystem::~Filesystem()
{
  delete root;
}

void Filesystem::mknod(const char *module, const char *name, u32 flag)
{
  if (!module || !name || !dev)
    return;

  File *device_node = dev->createChild(name, TYPE_DEVICE);
}

File *Filesystem::getRoot()
{
  return root;
}

File *Filesystem::path(const char *p)
{
  return resolve(p, nullptr, 0);
}

File *Filesystem::resolve(const char *p, char *fname, u32 size)
{
  if (fname && size) fname[0] = '\0';
  if (!p || !*p || !root) return nullptr;

  u32 length = 0;
  while (p[length])
  {
    if (++length >= kMaxPathLength) return nullptr;
  }

  File *fp = root;
  if (*p != '/' && arch.pcurrent)
  {
    File *current = arch.pcurrent->getCurrentDir();
    if (current) fp = current;
  }

  u32 links = 0;
  while (*p)
  {
    if (!fp || fp->getType() != TYPE_DIRECTORY) return nullptr;
    while (*p == '/') ++p;
    if (!*p) break;

    char name[File::kMaxNameLength + 1];
    u32 count = 0;
    while (*p && *p != '/')
    {
      if (count == File::kMaxNameLength) return nullptr;
      name[count++] = *p++;
    }
    name[count] = '\0';

    const char *remaining = p;
    while (*remaining == '/') ++remaining;
    if (fname && !*remaining)
    {
      if (*p || count + 1 > size || !strcmp(name, ".") || !strcmp(name, ".."))
        return nullptr;
      memcpy(fname, name, count + 1);
      return fp;
    }

    if (!strcmp(name, ".."))
    {
      if (fp != root && fp->getParent()) fp = fp->getParent();
    }
    else if (strcmp(name, "."))
    {
      fp->scan();
      fp = fp->find(name);
      while (fp && fp->getType() == TYPE_LINK)
      {
        if (++links > 32) return nullptr;
        fp = fp->getLink();
      }
      if (!fp) return nullptr;
    }
  }

  return fname ? nullptr : fp;
}

bool Filesystem::get_path(File *file, char *buffer, u32 size)
{
  if (!buffer || !size) return false;
  buffer[0] = '\0';
  if (!file || !root || size < 2) return false;

  u32 position = size - 1;
  buffer[position] = '\0';
  while (file != root)
  {
    if (!file || !file->getName())
    {
      buffer[0] = '\0';
      return false;
    }
    u32 length = strlen(file->getName());
    if (!length || length > File::kMaxNameLength || length + 1 > position)
    {
      buffer[0] = '\0';
      return false;
    }
    position -= length;
    memcpy(buffer + position, file->getName(), length);
    buffer[--position] = '/';
    file = file->getParent();
  }
  if (position == size - 1) buffer[--position] = '/';
  for (u32 i = position; i < size; ++i) buffer[i - position] = buffer[i];
  return true;
}

File *Filesystem::pivot_root(File *targetdir)
{
  if (!targetdir || targetdir->getType() != TYPE_DIRECTORY)
    return root;

  File *newRoot = new File("/", TYPE_DIRECTORY);

  File *child = targetdir->getChild();
  while (child)
  {
    File *next = child->getNext();
    newRoot->addChild(child);
    child = next;
  }

  return newRoot;
}

File *Filesystem::path_parent(const char *p, char *fname, u32 size)
{
  if (!fname || !size) return nullptr;
  return resolve(p, fname, size);
}

u32 Filesystem::link(const char *fname, const char *newf)
{
  File *target = path(fname);
  if (!target) return ERROR_PARAM;

  char name[File::kMaxNameLength + 1];
  File *parent = path_parent(newf, name, sizeof(name));
  if (!parent) return ERROR_PARAM;
  parent->scan();
  if (parent->find(name)) return ERROR_PARAM;

  File *node = new File(name, TYPE_LINK);
  if (!node || !node->getName())
  {
    delete node;
    return ERROR_MEMORY;
  }
  node->setLink(target);
  u32 result = parent->addChild(node);
  if (result != RETURN_OK) delete node;
  return result;
}

u32 Filesystem::addFile(const char *dir, File *fp)
{
  File *parent = path(dir);
  if (!parent)
    return ERROR_PARAM;

  return parent->addChild(fp);
}

bool Filesystem::register_driver(FilesystemDriver *driver)
{
  if (!driver || driver_count >= kMaxDrivers)
    return false;

  drivers[driver_count++] = driver;
  return true;
}

FilesystemDriver *Filesystem::find_driver(const char *name)
{
  if (!name)
    return nullptr;

  for (u32 i = 0; i < driver_count; ++i)
  {
    if (drivers[i] && strcmp(drivers[i]->name(), name) == 0)
      return drivers[i];
  }
  return nullptr;
}

bool Filesystem::mount(BlockDevice *device, const char *mount_path, const char *fs_name)
{
  if (!device || !mount_path || !fs_name)
    return false;

  if (mount_count >= kMaxMounts)
    return false;

  FilesystemDriver *driver = find_driver(fs_name);
  if (!driver)
    return false;

  File *mp = path(mount_path);
  if (!mp)
    return false;

  if (mp->getType() != TYPE_DIRECTORY)
    return false;

  File *final_mp = driver->mount(mp, device);
  if (!final_mp)
    return false;

  MountInfo &info = mounts[mount_count++];
  u32 len = strlen(mount_path);
  if (len >= sizeof(info.path))
    len = sizeof(info.path) - 1;
  memcpy(info.path, mount_path, len);
  info.path[len] = '\0';
  info.driver = driver;
  info.device = device;
  info.mount_point = final_mp;

  if (strcmp(mount_path, "/") == 0) {
    root = final_mp;
  }
  return true;
}

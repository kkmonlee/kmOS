#include <os.h>
#include <filesystem.h>
#include <filesystem_driver.h>
#include <core/block_device.h>
#include <memory_file.h>
#include <fcntl.h>

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
  if (this == &fsm) init_console();
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

void Filesystem::mknod(const char *module, const char *name, u32)
{
  if (!module || !name || !dev)
    return;

  dev->createChild(name, TYPE_DEVICE);
}

File *Filesystem::getRoot()
{
  return root;
}

File *Filesystem::path(const char *p, int *error, bool follow)
{
  return resolve(p, nullptr, 0, false, error, follow ? Lookup::Follow : Lookup::NoFollow);
}

int Filesystem::open(const char *name, u32 flags, u32 mode, File *&file) {
  IRQGuard guard;
  int error;
  bool exclusive = (flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL);
  file = path(name, &error, !exclusive);
  if (flags & O_DIRECTORY) {
    if (!file) return error;
    if (file->getType() != TYPE_DIRECTORY) return -20;
    return (flags & O_ACCMODE) == O_RDONLY ? 0 : -21;
  }
  if (file) {
    if (exclusive) return -17;
    return file->getType() == TYPE_FILE || file->getType() == TYPE_DEVICE ? 0 : -21;
  }
  if (error != -2 || !(flags & O_CREAT)) return error;
  char leaf[File::kMaxNameLength + 1];
  File *parent = resolve(name, leaf, sizeof(leaf), false, &error, exclusive ? Lookup::Follow : Lookup::Create);
  if (!parent) return error;
  int result = parent->createFile(leaf, mode, file);
  return result ? result : 1;
}

int Filesystem::unlink(const char *path) {
  IRQGuard guard;
  char leaf[File::kMaxNameLength + 1];
  int error;
  File *parent = resolve(path, leaf, sizeof(leaf), false, &error);
  if (!parent) return error;
  return parent->unlinkFile(leaf);
}

int Filesystem::rename(const char *source, const char *destination) {
  IRQGuard guard;
  auto parent_of = [&](const char *name, char *leaf, int &error) -> File * {
    if (!name || !*name) { error = -2; return nullptr; }
    u32 end = strlen(name);
    while (end && name[end - 1] == '/') --end;
    if (!end) { error = -16; return nullptr; }
    u32 start = end;
    while (start && name[start - 1] != '/') --start;
    if ((end - start == 1 && name[start] == '.') ||
        (end - start == 2 && name[start] == '.' && name[start + 1] == '.')) {
      error = -22;
      return nullptr;
    }
    return resolve(name, leaf, File::kMaxNameLength + 1, true, &error);
  };

  int error;
  char old_name[File::kMaxNameLength + 1], new_name[File::kMaxNameLength + 1];
  File *from = parent_of(source, old_name, error);
  if (!from) return error;
  if (from->isRemoved()) return -2;
  from->scan();
  File *file = from->find(old_name);
  if (!file) return -2;
  bool directory = file->getType() == TYPE_DIRECTORY;
  if (!directory && source[strlen(source) - 1] == '/') return -20;

  File *to = parent_of(destination, new_name, error);
  if (!to) return error;
  if (to->isRemoved()) return -2;
  to->scan();
  File *target = to->find(new_name);
  if (destination[strlen(destination) - 1] == '/' &&
      (!directory || (target && target->getType() != TYPE_DIRECTORY))) return -20;
  for (u32 i = 0; i < mount_count; ++i) {
    if (mounts[i].mount_point == file || mounts[i].mount_point == target) return -16;
  }

  const MountInfo *mount = find_mount(from);
  if (mount != find_mount(to)) return -18;
  return mount ? mount->driver->rename(*file, *to, new_name) : -30;
}

const Filesystem::MountInfo *Filesystem::find_mount(File *file) const {
  for (; file; file = file->getParent()) {
    for (u32 i = 0; i < mount_count; ++i)
      if (mounts[i].mount_point == file) return &mounts[i];
  }
  return nullptr;
}

int Filesystem::hard_link(const char *source, const char *destination) {
  IRQGuard guard;
  int error;
  File *file = path(source, &error, false);
  if (!file) return error;
  if (file->getType() == TYPE_DIRECTORY) return -1;
  char leaf[File::kMaxNameLength + 1];
  File *parent = resolve(destination, leaf, sizeof(leaf), false, &error);
  if (!parent) return error;
  const MountInfo *mount = find_mount(parent);
  if (mount != find_mount(file)) return -18;
  return mount ? mount->driver->link(*file, *parent, leaf) : -30;
}

int Filesystem::symlink(const char *target, const char *destination) {
  IRQGuard guard;
  if (!target || !*target) return -2;
  if ((u32)strlen(target) >= kMaxPathLength) return -36;
  int error;
  char leaf[File::kMaxNameLength + 1];
  File *parent = resolve(destination, leaf, sizeof(leaf), false, &error);
  if (!parent) return error;
  const MountInfo *mount = find_mount(parent);
  return mount ? mount->driver->symlink(*parent, leaf, target) : -30;
}

int Filesystem::mkdir(const char *path, u32 mode) {
  IRQGuard guard;
  int error;
  if (this->path(path, &error, false)) return -17;
  if (error != -2) return error;
  char leaf[File::kMaxNameLength + 1];
  File *parent = resolve(path, leaf, sizeof(leaf), true, &error);
  if (!parent) return error;
  return parent->makeDirectory(leaf, mode);
}

int Filesystem::rmdir(const char *path) {
  IRQGuard guard;
  if (!path || !*path) return -2;
  u32 length = strlen(path);
  while (length && path[length - 1] == '/') --length;
  if (!length) return -16;
  u32 start = length;
  while (start && path[start - 1] != '/') --start;
  if ((length - start == 1 && path[start] == '.') ||
      (length - start == 2 && path[start] == '.' && path[start + 1] == '.')) return -22;
  char leaf[File::kMaxNameLength + 1];
  int error;
  File *parent = resolve(path, leaf, sizeof(leaf), true, &error);
  if (!parent) return error;
  parent->scan();
  File *target = parent->find(leaf);
  if (!target) return -2;
  if (target == root) return -16;
  for (u32 i = 0; i < mount_count; ++i)
    if (mounts[i].mount_point == target) return -16;
  if (target->getType() != TYPE_DIRECTORY) return -20;
  return parent->removeDirectory(leaf);
}

File *Filesystem::resolve(const char *p, char *fname, u32 size, bool trailing, int *error,
                          Lookup lookup)
{
  if (error) *error = -2;
  if (fname && size) fname[0] = '\0';
  if (!p || !*p || !root) return nullptr;

  u32 length = 0;
  while (p[length])
  {
    if (++length >= kMaxPathLength) {
      if (error) *error = -36;
      return nullptr;
    }
  }

  File *fp = root;
  if (*p != '/' && arch.pcurrent)
  {
    File *current = arch.pcurrent->getCurrentDir();
    if (current) fp = current;
  }

  struct PathBuffers {
    char *data = nullptr;
    ~PathBuffers() { if (data) kfree(data); }
  } buffers;
  char *expanded = nullptr;
  u32 links = 0;
  while (*p)
  {
    if (fp->getType() != TYPE_DIRECTORY) {
      if (error) *error = -20;
      return nullptr;
    }
    while (*p == '/') ++p;
    if (!*p) break;

    char name[File::kMaxNameLength + 1];
    u32 count = 0;
    while (*p && *p != '/')
    {
      if (count == File::kMaxNameLength) {
        if (error) *error = -36;
        return nullptr;
      }
      name[count++] = *p++;
    }
    name[count] = '\0';

    const char *remaining = p;
    while (*remaining == '/') ++remaining;
    if (fname && lookup != Lookup::Create && !*remaining && (!*p || trailing))
    {
      if (count + 1 > size || !strcmp(name, ".") || !strcmp(name, ".."))
        return nullptr;
      memcpy(fname, name, count + 1);
      if (error) *error = 0;
      return fp;
    }

    if (!strcmp(name, ".."))
    {
      if (fp != root && fp->getParent()) fp = fp->getParent();
    }
    else if (strcmp(name, "."))
    {
      fp->scan();
      File *child = fp->find(name);
      if (lookup == Lookup::Create && !child && !*p && count + 1 <= size) {
        memcpy(fname, name, count + 1);
        if (error) *error = 0;
        return fp;
      }
      while (child && (child->getType() == TYPE_LINK ||
                       (child->getType() == TYPE_SYMLINK && (lookup != Lookup::NoFollow || *p)))) {
        if (++links > 32) {
          if (error) *error = -40;
          return nullptr;
        }
        if (child->getType() == TYPE_LINK) {
          child = child->getLink();
          continue;
        }
        if (!buffers.data) {
          buffers.data = (char *)kmalloc(2 * kMaxPathLength);
          if (!buffers.data) {
            if (error) *error = -12;
            return nullptr;
          }
        }
        char *next = expanded == buffers.data ? buffers.data + kMaxPathLength : buffers.data;
        int target = child->readLink(next, kMaxPathLength - 1);
        if (target <= 0) {
          if (error) *error = target ? target : -2;
          return nullptr;
        }
        u32 suffix = strlen(p);
        if ((u32)target + suffix >= kMaxPathLength) {
          if (error) *error = -36;
          return nullptr;
        }
        memcpy(next + target, p, suffix + 1);
        fp = *next == '/' ? root : child->getParent();
        if (!fp) return nullptr;
        p = expanded = next;
        child = fp;
        break;
      }
      if (!child) return nullptr;
      fp = child;
    }
  }

  if (fname) return nullptr;
  if (error) *error = 0;
  return fp;
}

int Filesystem::get_path(File *file, char *buffer, u32 size)
{
  IRQGuard guard;
  if (!buffer || !size) return -34;
  buffer[0] = '\0';
  if (!file || !root) return -2;
  if (size < 2) return -34;

  u32 position = size - 1;
  buffer[position] = '\0';
  while (file != root)
  {
    if (!file || file->isRemoved() || !file->getName())
    {
      buffer[0] = '\0';
      return -2;
    }
    u32 length = strlen(file->getName());
    if (!length || length > File::kMaxNameLength || length + 1 > position)
    {
      buffer[0] = '\0';
      return length + 1 > position ? -34 : -2;
    }
    position -= length;
    memcpy(buffer + position, file->getName(), length);
    buffer[--position] = '/';
    file = file->getParent();
  }
  if (position == size - 1) buffer[--position] = '/';
  for (u32 i = position; i < size; ++i) buffer[i - position] = buffer[i];
  return size - position;
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

File *Filesystem::path_parent(const char *p, char *fname, u32 size, bool trailing)
{
  if (!fname || !size) return nullptr;
  return resolve(p, fname, size, trailing, nullptr);
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

bool Filesystem::mount(BlockDevice *device, const char *mount_path, const char *fs_name, bool writable)
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

  File *final_mp = driver->mount(mp, device, writable);
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

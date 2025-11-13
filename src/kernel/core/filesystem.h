#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <runtime/types.h>
#include <core/file.h>

class BlockDevice;
class FilesystemDriver;

class Filesystem
{
public:
  Filesystem();
  ~Filesystem();

  void init();
  void mknod(const char *module, const char *name, u32 flag);

  File *path(const char *p);
  File *path_parent(const char *p, char *fname);

  u32 link(const char *fname, const char *newf);
  u32 addFile(const char *dir, File *fp);

  bool register_driver(FilesystemDriver *driver);
  bool mount(BlockDevice *device, const char *path, const char *fs_name);
  FilesystemDriver *find_driver(const char *name);

  File *pivot_root(File *targetdir);
  File *getRoot();

private:
  struct MountInfo {
    char path[128];
    FilesystemDriver *driver;
    BlockDevice *device;
    File *mount_point;
  };

  static const u32 kMaxDrivers = 8;
  static const u32 kMaxMounts = 8;

  FilesystemDriver *drivers[kMaxDrivers];
  u32 driver_count;
  MountInfo mounts[kMaxMounts];
  u32 mount_count;

  File *root; // root directory
  File *dev;  // device directory
  File *var;  // variable directory
};

extern Filesystem fsm;

#endif // FILESYSTEM_H

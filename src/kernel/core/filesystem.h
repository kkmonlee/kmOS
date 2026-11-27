#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <runtime/types.h>
#include <core/file.h>

class BlockDevice;
class FilesystemDriver;

class Filesystem
{
public:
  static const u32 kMaxPathLength = 4096;

  Filesystem();
  ~Filesystem();

  void init();
  void mknod(const char *module, const char *name, u32 flag);

  File *path(const char *p, int *error = nullptr, bool follow = true);
  int open(const char *path, u32 flags, u32 mode, File *&file);
  int unlink(const char *path);
  int rename(const char *source, const char *destination);
  int hard_link(const char *source, const char *destination);
  int symlink(const char *target, const char *destination);
  int mkdir(const char *path, u32 mode);
  int rmdir(const char *path);
  File *path_parent(const char *p, char *fname, u32 size, bool trailing = false);
  int get_path(File *file, char *buffer, u32 size);

  u32 link(const char *fname, const char *newf);
  u32 addFile(const char *dir, File *fp);

  bool register_driver(FilesystemDriver *driver);
  bool mount(BlockDevice *device, const char *path, const char *fs_name, bool writable = false);
  FilesystemDriver *find_driver(const char *name);

  File *pivot_root(File *targetdir);
  File *getRoot();

private:
  enum class Lookup { Follow, NoFollow, Create };
  File *resolve(const char *p, char *fname, u32 size, bool trailing, int *error,
                Lookup lookup = Lookup::Follow);

  struct MountInfo {
    char path[128];
    FilesystemDriver *driver;
    BlockDevice *device;
    File *mount_point;
  };

  const MountInfo *find_mount(File *file) const;

  static const u32 kMaxDrivers = 8;
  static const u32 kMaxMounts = 8;

  FilesystemDriver *drivers[kMaxDrivers];
  u32 driver_count;
  MountInfo mounts[kMaxMounts];
  u32 mount_count;

  File *root;
  File *dev;
  File *var;
};

extern Filesystem fsm;

#endif

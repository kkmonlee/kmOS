#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <runtime/types.h>
#include <core/file.h>

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

  File *pivot_root(File *targetdir);
  File *getRoot();

private:
  File *root; // root directory
  File *dev;  // device directory
  File *var;  // variable directory
};

extern Filesystem fsm;

#endif // FILESYSTEM_H

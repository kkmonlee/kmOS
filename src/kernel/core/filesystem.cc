#include <os.h>
#include <filesystem.h>

extern "C" {
    void *memcpy(void *dest, const void *src, int n);
    int strcmp(const char *s1, const char *s2);
    int strcpy(char *dst, const char *src);
}

#include <arch/x86/architecture.h>

// class to manage file hierarchy

Filesystem::Filesystem() {}

void Filesystem::init()
{
  root = new File("/", TYPE_DIRECTORY);

  dev = root->createChild("dev", TYPE_DIRECTORY);        // device folder
  root->createChild("proc", TYPE_DIRECTORY);             // process folder
  root->createChild("mnt", TYPE_DIRECTORY);              // mount points folder
  File *sysd = root->createChild("sys", TYPE_DIRECTORY); // system folder
  var = sysd->createChild("env", TYPE_DIRECTORY);        // environment variables
  sysd->createChild("usr", TYPE_DIRECTORY);              // user data
  sysd->createChild("mods", TYPE_DIRECTORY);             // modules
  sysd->createChild("sockets", TYPE_DIRECTORY);          // sockets
}

Filesystem::~Filesystem()
{
  delete root;
}

void Filesystem::mknod(const char *module, const char *name, u32 flag)
{
}

File *Filesystem::getRoot()
{
  return root;
}

File *Filesystem::path(const char *p)
{
  if (!p)
    return NULL;

  File *fp = root;
  char *name = NULL;
  const char *beg_p = p;
  const char *end_p = beg_p;

  if (p[0] != '/' && arch.pcurrent != NULL)
  {
    fp = arch.pcurrent->getCurrentDir();
  }

  while (*beg_p == '/')
    beg_p++;
  end_p++;

  while (*beg_p != '\0')
  {
    if (fp->getType() != TYPE_DIRECTORY)
      return NULL;

    while (*end_p != '\0' && *end_p != '/')
      end_p++;

    name = (char *)kmalloc(end_p - beg_p + 1);
    memcpy(name, beg_p, end_p - beg_p);
    name[end_p - beg_p] = '\0';

    if (strcmp("..", name) == 0)
    {
      fp = fp->getParent();
    }
    else if (strcmp(".", name) != 0)
    {
      fp->scan();
      File *found = fp->find(name);
      kfree(name);

      if (!found)
        return NULL;

      if (found->getType() == TYPE_LINK && found->getLink() != NULL)
      {
        fp = found->getLink();
      }
      else
      {
        fp = found;
      }
    }

    beg_p = end_p;
    while (*beg_p == '/')
      beg_p++;
    end_p++;
  }

  return fp;
}

File *Filesystem::pivot_root(File *targetdir)
{
  if (!targetdir)
    return root;

  File *newRoot = new File("/", TYPE_DIRECTORY);

  File *child = targetdir->getChild();
  while (child)
  {
    newRoot->addChild(child);
    child = child->getNext();
  }

  return newRoot;
}

File *Filesystem::path_parent(const char *p, char *fname)
{
  if (!p)
    return NULL;

  File *fp = root;
  char *name = NULL;
  const char *beg_p = p;
  const char *end_p = beg_p;

  while (*beg_p == '/')
    beg_p++;
  end_p++;

  while (*beg_p != '\0')
  {
    if (fp->getType() != TYPE_DIRECTORY)
      return NULL;

    while (*end_p != '\0' && *end_p != '/')
      end_p++;

    name = (char *)kmalloc(end_p - beg_p + 1);
    memcpy(name, beg_p, end_p - beg_p);
    name[end_p - beg_p] = '\0';

    if (strcmp("..", name) == 0)
    {
      fp = fp->getParent();
    }
    else if (strcmp(".", name) != 0)
    {
      File *found = fp->find(name);
      kfree(name);

      if (!found)
      {
        strcpy(fname, name);
        return fp;
      }

      fp = found->getType() == TYPE_LINK && found->getLink() ? found->getLink() : found;
    }

    beg_p = end_p;
    while (*beg_p == '/')
      beg_p++;
    end_p++;
  }

  return fp;
}

u32 Filesystem::link(const char *fname, const char *newf)
{
  File *tolink = path(fname);
  if (!tolink)
    return -1;

  char *nname = (char *)kmalloc(255);
  File *parent = path_parent(newf, nname);
  File *linkFile = new File(nname, TYPE_LINK);
  linkFile->setLink(tolink);
  parent->addChild(linkFile);

  return RETURN_OK;
}

u32 Filesystem::addFile(const char *dir, File *fp)
{
  File *parent = path(dir);
  if (!parent)
    return ERROR_PARAM;

  return parent->addChild(fp);
}

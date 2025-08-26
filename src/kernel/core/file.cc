#include <os.h>
#include <file.h>
#include <runtime/alloc.h>
#include <arch/x86/architecture.h>

extern "C" {
    void pd_add_page(char *v_addr, char *p_addr, u32 flags, struct page_directory *pd);
}

struct page {
    char *p_addr;
    char *v_addr;
    list_head list;
};

struct page_directory;

extern "C" {
    int strlen(const char *s);
    void *memcpy(void *dest, const void *src, int n);
    void *memset(void *s, int c, int n);
    int strcmp(const char *s1, const char *s2);
    void pd_add_page(char *v_addr, char *p_addr, u32 flags, struct page_directory *pd);
}

static void strreplace(char *s, char a, char to)
{
  if (s == NULL)
    return;
  while (*s)
  {
    if (*s == a)
      *s = to;
    s++;
  }
}

u32 File::inode_system = 0; // Starting inode number

/* Constructor */
File::File(const char *n, u8 t)
{
  name = (char *)kmalloc(strlen(n) + 1);
  memset(name, 0, strlen(n) + 1);
  memcpy(name, n, strlen(n));

  checkName();
  master = arch.pcurrent; // The current process is the master upon creation
  inode = inode_system++;
  size = 0;
  type = t;
  parent = child = next = prec = link = NULL;
  map_memory = NULL;
}

/* Destructor */
File::~File()
{
  kfree(name);

  // Update sibling relationships
  if (prec == NULL)
  {
    parent->setChild(next);
    if (next)
      next->setPrec(NULL);
  }
  else if (next == NULL)
  {
    prec->setNext(NULL);
  }
  else
  {
    prec->setNext(next);
    next->setPrec(prec);
  }

  // Delete child files/directories
  File *n = child;
  while (n != NULL)
  {
    File *nextChild = n->getNext();
    delete n;
    n = nextChild;
  }
}

#define CHAR_REPLACEMENT '_'

/**
 * Ensures the file name does not contain invalid characters by replacing them.
 */
void File::checkName()
{
  const char invalidChars[] = {'/', '\\', '?', ':', '>', '<', '*', '"'};
  for (char c : invalidChars)
  {
    strreplace(name, c, CHAR_REPLACEMENT);
  }
}

/**
 * Adds a child to this file/directory.
 */
u32 File::addChild(File *n)
{
  if (!n)
    return PARAM_NULL;

  n->setParent(this);
  n->setPrec(NULL);
  n->setNext(child);
  if (child != NULL)
    child->setPrec(n);
  child = n;

  return RETURN_OK;
}

/**
 * Creates and adds a child to this file/directory.
 */
File *File::createChild(const char *n, u8 t)
{
  File *newFile = new File(n, t);
  addChild(newFile);
  return newFile;
}

File *File::getParent() { return parent; }
File *File::getChild() { return child; }
File *File::getNext() { return next; }
File *File::getPrec() { return prec; }
File *File::getLink() { return link; }
u32 File::getSize() { return size; }
u32 File::getInode() { return inode; }
u8 File::getType() { return type; }
char *File::getName() { return name; }

void File::setType(u8 t) { type = t; }
void File::setSize(u32 t) { size = t; }
void File::setParent(File *n) { parent = n; }
void File::setLink(File *n) { link = n; }
void File::setChild(File *n) { child = n; }
void File::setNext(File *n) { next = n; }
void File::setPrec(File *n) { prec = n; }

/**
 * Sets a new name for the file, ensuring it complies with naming rules.
 */
void File::setName(char *n)
{
  kfree(name);
  name = (char *)kmalloc(strlen(n) + 1);
  memcpy(name, n, strlen(n) + 1);
  checkName();
}

/**
 * Finds a child file/directory by name.
 */
File *File::find(char *n)
{
  File *fp = child;
  while (fp != NULL)
  {
    if (!strcmp(fp->getName(), n))
      return fp;
    fp = fp->getNext();
  }
  return NULL;
}

// Placeholder implementations for file operations
u32 File::open(u32 /*flag*/) { return NOT_DEFINED; }
u32 File::close() { return NOT_DEFINED; }
u32 File::read(u32 /*pos*/, u8 * /*buffer*/, u32 /*size*/) { return NOT_DEFINED; }
u32 File::write(u32 /*pos*/, u8 * /*buffer*/, u32 /*size*/) { return NOT_DEFINED; }
u32 File::ioctl(u32 /*id*/, u8 * /*buffer*/) { return NOT_DEFINED; }
u32 File::remove()
{
  delete this;
  return NOT_DEFINED;
}

/**
 * Returns the file statistics (placeholder implementation).
 */
stat_fs File::stat()
{
  stat_fs st;
  return st;
}

void File::scan() {
}

/**
 * Memory maps the file if possible.
 */
u32 File::mmap(u32 sizee, u32 /*flags*/, u32 /*offset*/, u32 /*prot*/)
{
  if (map_memory != NULL)
  {
    unsigned int address;
    struct page *pg;
    process_st *current = arch.pcurrent->getPInfo();

    for (u32 i = 0; i < sizee; ++i)
    {
      address = (unsigned int)(map_memory + i * PAGESIZE);

      pg = (struct page *)kmalloc(sizeof(struct page));
      pg->p_addr = (char *)(address);
      pg->v_addr = (char *)(address & 0xFFFFF000);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"
      list_add(&pg->list, &current->pglist);
#pragma GCC diagnostic pop
      pd_add_page(pg->v_addr, pg->p_addr, PG_USER, current->pd);
    }
    return (u32)map_memory;
  }
  else
  {
    return -1;
  }
}

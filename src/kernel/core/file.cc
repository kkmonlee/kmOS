#include <os.h>
#include <file.h>
#include <runtime/alloc.h>
#include <arch/x86/architecture.h>
#include <dirent.h>
#include <sys/stat.h>

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

u32 File::inode_system = 0;
u64 File::next_entry_cookie = 3;

File::File(const char *n, u8 t, bool sanitize)
{
  name = n ? (char *)kmalloc(strlen(n) + 1) : nullptr;
  if (name) memcpy(name, n, strlen(n) + 1);

  if (sanitize) checkName();
  master = arch.pcurrent;
  inode = inode_system++;
  references = 0;
  removed = false;
  size = 0;
  type = t;
  parent = child = next = prec = link = NULL;
  link_users = link_next = link_prec = nullptr;
  map_memory = NULL;
}

File::~File()
{
  setLink(nullptr);
  while (link_users) link_users->setLink(nullptr);
  detach();

  while (child) child->File::remove();
  kfree(name);
}

void File::detach()
{
  if (prec) prec->next = next;
  else if (parent) parent->child = next;
  if (next) next->prec = prec;
  parent = next = prec = nullptr;
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
  if (type != TYPE_DIRECTORY || !n->name || !*n->name)
    return ERROR_PARAM;
  for (File *ancestor = this; ancestor; ancestor = ancestor->parent)
  {
    if (ancestor == n) return ERROR_PARAM;
  }
  File *existing = find(n->name);
  if (existing && existing != n) return ERROR_PARAM;
  if (n->parent == this) return RETURN_OK;

  n->detach();
  n->entry_cookie = next_entry_cookie++;
  n->removed = false;
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
  if (!n || !*n || strlen(n) > (int)kMaxNameLength ||
      !strcmp(n, ".") || !strcmp(n, "..")) return nullptr;
  for (const char *p = n; *p; ++p)
  {
    if (*p == '/') return nullptr;
  }
  File *newFile = new File(n, t);
  if (!newFile || addChild(newFile) != RETURN_OK)
  {
    delete newFile;
    return nullptr;
  }
  return newFile;
}

File *File::getParent() { return parent; }
File *File::getChild() { return child; }
File *File::getNext() { return next; }
File *File::getPrec() { return prec; }
File *File::getLink() { return link; }
u32 File::getSize() { return size; }
u32 File::getInode() const { return inode; }
u8 File::getType() { return type; }
char *File::getName() { return name; }

void File::setType(u8 t) { type = t; }
void File::setSize(u32 t) { size = t; }
void File::setParent(File *n) { parent = n; }
void File::setLink(File *n)
{
  if (link == n) return;
  if (link)
  {
    if (link_prec) link_prec->link_next = link_next;
    else link->link_users = link_next;
    if (link_next) link_next->link_prec = link_prec;
  }
  link = n;
  link_prec = nullptr;
  link_next = n ? n->link_users : nullptr;
  if (link_next) link_next->link_prec = this;
  if (n) n->link_users = this;
}
void File::setChild(File *n) { child = n; }
void File::setNext(File *n) { next = n; }
void File::setPrec(File *n) { prec = n; }

/**
 * Sets a new name for the file, ensuring it complies with naming rules.
 */
void File::setName(const char *n)
{
  if (!n || !*n || strlen(n) > (int)kMaxNameLength) return;
  char *replacement = (char *)kmalloc(strlen(n) + 1);
  if (!replacement) return;
  memcpy(replacement, n, strlen(n) + 1);
  kfree(name);
  name = replacement;
  checkName();
}

/**
 * Finds a child file/directory by name.
 */
File *File::find(const char *n)
{
  if (!n) return nullptr;
  File *fp = child;
  while (fp != NULL)
  {
    if (!strcmp(fp->getName(), n))
      return fp;
    fp = fp->getNext();
  }
  return NULL;
}

u32 File::open(u32 flag) { return type == TYPE_DIRECTORY && !flag ? RETURN_OK : NOT_DEFINED; }
u32 File::close() { return type == TYPE_DIRECTORY ? RETURN_OK : NOT_DEFINED; }
u32 File::read(u32 /*pos*/, u8 * /*buffer*/, u32 /*size*/) { return NOT_DEFINED; }
u32 File::write(u32 /*pos*/, u8 * /*buffer*/, u32 /*size*/) { return NOT_DEFINED; }
u32 File::append(u8 *, u32, u32 &) { return (u32)-22; }
u32 File::ioctl(u32 /*id*/, u8 * /*buffer*/) { return NOT_DEFINED; }
int File::createFile(const char *, u32, File *&file) {
  file = nullptr;
  return -30;
}

int File::unlinkFile(const char *) { return -30; }
int File::makeDirectory(const char *, u32) { return -30; }
int File::removeDirectory(const char *) { return -30; }
int File::truncate(u32) { return -30; }

u32 File::remove()
{
  u32 flags = irq_save();
  detach();
  removed = true;
  if (!references) delete this;
  irq_restore(flags);
  return RETURN_OK;
}

void File::retain() {
  u32 flags = irq_save();
  ++references;
  irq_restore(flags);
}

void File::release() {
  u32 flags = irq_save();
  if (references && !--references && removed) delete this;
  irq_restore(flags);
}

static u32 directory_type(u8 type) {
  switch (type) {
  case TYPE_FILE: return DT_REG;
  case TYPE_DIRECTORY: return DT_DIR;
  case TYPE_DEVICE: return DT_CHR;
  case TYPE_LINK: return DT_LNK;
  case TYPE_PIPE: return DT_FIFO;
  default: return DT_UNKNOWN;
  }
}

static void directory_entry(struct dirent &entry, File *file, const char *name) {
  entry = {};
  entry.d_ino = file->getInode();
  entry.d_type = directory_type(file->getType());
  for (u32 i = 0; i + 1 < sizeof(entry.d_name) && name[i]; ++i) entry.d_name[i] = name[i];
}

int File::readDirectory(u64 &position, struct dirent *entries, u32 count) {
  if (type != TYPE_DIRECTORY) return -20;
  if (removed) return -2;
  scan();
  u32 written = 0;
  while (position < 2 && written < count) {
    File *up = getParent();
    if (!position) directory_entry(entries[written++], this, ".");
    else directory_entry(entries[written++], up ? up : this, "..");
    ++position;
  }
  for (File *item = child; item && written < count; item = item->next) {
    if (position > 2 && item->entry_cookie >= position) continue;
    directory_entry(entries[written++], item, item->name);
    position = item->entry_cookie;
  }
  return written;
}

int File::stat(struct stat &result) {
  result = {};
  result.st_ino = getInode();
  result.st_mode = (directory_type(type) << 12) | (type == TYPE_DIRECTORY ? 0555 : 0444);
  if (type == TYPE_DEVICE || type == TYPE_PIPE) result.st_mode |= 0222;
  result.st_nlink = removed ? 0 : 1;
  result.st_size = getSize();
  result.st_blksize = 4096;
  return 0;
}

void File::scan() {
}

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

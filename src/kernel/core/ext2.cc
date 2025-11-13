#include <os.h>
#include <core/ext2.h>
#include <core/block_device.h>
#include <runtime/alloc.h>

extern "C" {
    void *memcpy(void *dest, const void *src, int n);
    void *memset(void *s, int c, int n);
    int strcmp(const char *s1, const char *s2);
}

static constexpr u16 EXT2_SUPER_MAGIC = 0xEF53;
static constexpr u32 EXT2_ROOT_INO = 2;
static constexpr u8 EXT2_FT_REG_FILE = 1;
static constexpr u8 EXT2_FT_DIR = 2;

class Ext2Mount {
public:
    explicit Ext2Mount(BlockDevice *device)
        : device_(device), groups_(nullptr), group_count_(0),
          block_size_(1024), inode_size_(128), ptrs_per_block_(256) {}

    ~Ext2Mount() {
        if (groups_) {
            kfree(groups_);
        }
    }

    bool initialize() {
        if (!device_)
            return false;

        if (device_->read(1024, (u8 *)&super_, sizeof(super_)) != RETURN_OK)
            return false;

        if (super_.s_magic != EXT2_SUPER_MAGIC)
            return false;

        block_size_ = 1024u << super_.s_log_block_size;
        inode_size_ = super_.s_inode_size ? super_.s_inode_size : 128;
        ptrs_per_block_ = block_size_ / sizeof(u32);

        group_count_ = (super_.s_blocks_count + super_.s_blocks_per_group - 1) / super_.s_blocks_per_group;
        if (group_count_ == 0)
            return false;

        groups_ = (ext2_group_desc *)kmalloc(group_count_ * sizeof(ext2_group_desc));
        if (!groups_)
            return false;

        u32 table_size = group_count_ * sizeof(ext2_group_desc);
        u8 *temp = (u8 *)kmalloc(block_size_);
        if (!temp)
            return false;

        u32 first_gdt_block = (block_size_ == 1024) ? 2 : 1;
        u32 remaining = table_size;
        u32 dest = 0;
        u32 block = first_gdt_block;
        while (remaining > 0) {
            if (device_->read(block * block_size_, temp, block_size_) != RETURN_OK) {
                kfree(temp);
                return false;
            }
            u32 copy = remaining < block_size_ ? remaining : block_size_;
            memcpy((u8 *)groups_ + dest, temp, copy);
            dest += copy;
            remaining -= copy;
            block++;
        }

        kfree(temp);
        return true;
    }

    BlockDevice *device() const { return device_; }
    u32 block_size() const { return block_size_; }

    bool read_inode(u32 ino, ext2_inode *out) {
        if (!out || ino == 0)
            return false;

        u32 index = ino - 1;
        u32 group = index / super_.s_inodes_per_group;
        if (group >= group_count_)
            return false;

        u32 index_in_group = index % super_.s_inodes_per_group;
        u32 inode_table_block = groups_[group].bg_inode_table;
        u32 offset = index_in_group * inode_size_;
        u64 byte_offset = (u64)inode_table_block * block_size_ + offset;

        u8 *temp = (u8 *)kmalloc(inode_size_);
        if (!temp)
            return false;

        if (device_->read((u32)byte_offset, temp, inode_size_) != RETURN_OK) {
            kfree(temp);
            return false;
        }

        memcpy(out, temp, sizeof(ext2_inode));
        kfree(temp);
        return true;
    }

    bool get_block(const ext2_inode &inode, u32 block_index, u32 &block_number) {
        if (block_index < 12) {
            block_number = inode.i_block[block_index];
            return block_number != 0;
        }

        block_index -= 12;
        if (block_index < ptrs_per_block_) {
            u32 indirect = inode.i_block[12];
            if (indirect == 0)
                return false;

            u32 *entries = (u32 *)kmalloc(block_size_);
            if (!entries)
                return false;

            if (device_->read(indirect * block_size_, (u8 *)entries, block_size_) != RETURN_OK) {
                kfree(entries);
                return false;
            }

            block_number = entries[block_index];
            kfree(entries);
            return block_number != 0;
        }

        // Double and triple indirect blocks not supported yet
        return false;
    }

    u32 read_file_data(const ext2_inode &inode, u32 offset, u8 *buffer, u32 size) {
        if (!buffer || size == 0)
            return 0;

        if (offset >= inode.i_size)
            return 0;

        if (offset + size > inode.i_size)
            size = inode.i_size - offset;

        u32 total_read = 0;
        u32 block_index = offset / block_size_;
        u32 block_offset = offset % block_size_;

        u8 *block_buffer = (u8 *)kmalloc(block_size_);
        if (!block_buffer)
            return 0;

        while (size > 0) {
            u32 block_number;
            if (!get_block(inode, block_index, block_number))
                break;

            if (device_->read(block_number * block_size_, block_buffer, block_size_) != RETURN_OK)
                break;

            u32 chunk = block_size_ - block_offset;
            if (chunk > size)
                chunk = size;

            memcpy(buffer + total_read, block_buffer + block_offset, chunk);

            size -= chunk;
            total_read += chunk;
            block_index++;
            block_offset = 0;
        }

        kfree(block_buffer);
        return total_read;
    }

    bool load_directory(Ext2Directory *dir);

private:
    BlockDevice *device_;
    ext2_super_block super_;
    ext2_group_desc *groups_;
    u32 group_count_;
    u32 block_size_;
    u32 inode_size_;
    u32 ptrs_per_block_;
};

bool Ext2Mount::load_directory(Ext2Directory *dir) {
    if (!dir)
        return false;

    ext2_inode inode;
    if (!read_inode(dir->inode(), &inode))
        return false;

    dir->setSize(inode.i_size);

    u32 total = inode.i_size;
    u32 processed = 0;
    u32 block_index = 0;

    u8 *block_buffer = (u8 *)kmalloc(block_size_);
    if (!block_buffer)
        return false;

    while (processed < total) {
        u32 block_number;
        if (!get_block(inode, block_index, block_number))
            break;

        if (device_->read(block_number * block_size_, block_buffer, block_size_) != RETURN_OK)
            break;

        u32 offset = 0;
        while (offset < block_size_ && processed < total) {
            ext2_dir_entry *entry = (ext2_dir_entry *)(block_buffer + offset);
            if (entry->rec_len == 0)
                break;

            if (entry->inode != 0 && entry->name_len > 0) {
                char name[256];
                u32 name_len = entry->name_len;
                if (name_len >= sizeof(name))
                    name_len = sizeof(name) - 1;
                memcpy(name, entry->name, name_len);
                name[name_len] = '\0';

                if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                    ext2_inode child_inode;
                    if (read_inode(entry->inode, &child_inode)) {
                        if (entry->file_type == EXT2_FT_DIR) {
                            Ext2Directory *child = new Ext2Directory(name, dir->mount(), entry->inode, false);
                            child->setSize(child_inode.i_size);
                            dir->addChild(child);
                        } else if (entry->file_type == EXT2_FT_REG_FILE || entry->file_type == 0) {
                            Ext2RegularFile *child = new Ext2RegularFile(name, dir->mount(), entry->inode, child_inode.i_size);
                            dir->addChild(child);
                        }
                    }
                }
            }

            processed += entry->rec_len;
            offset += entry->rec_len;
        }

        block_index++;
    }

    kfree(block_buffer);
    return true;
}

Ext2Filesystem ext2_driver;

Ext2Node::Ext2Node(const char *name, u8 type, Ext2Mount *mount, u32 inode)
    : File(name, type), mount_(mount), inode_(inode) {
}

Ext2Node::~Ext2Node() = default;

Ext2Directory::Ext2Directory(const char *name, Ext2Mount *mount, u32 inode, bool owns_mount)
    : Ext2Node(name, TYPE_DIRECTORY, mount, inode), loaded_(false), owns_mount_(owns_mount) {
}

Ext2Directory::~Ext2Directory() {
    if (owns_mount_ && mount_) {
        delete mount_;
        mount_ = nullptr;
    }
}

void Ext2Directory::scan() {
    if (loaded_ || !mount_)
        return;

    if (mount_->load_directory(this)) {
        loaded_ = true;
    }
}

Ext2RegularFile::Ext2RegularFile(const char *name, Ext2Mount *mount, u32 inode, u32 size)
    : Ext2Node(name, TYPE_FILE, mount, inode) {
    setSize(size);
}

u32 Ext2RegularFile::read(u32 pos, u8 *buffer, u32 size) {
    if (!mount_)
        return 0;

    ext2_inode inode_data;
    if (!mount_->read_inode(inode_, &inode_data))
        return 0;

    return mount_->read_file_data(inode_data, pos, buffer, size);
}

Ext2Filesystem::Ext2Filesystem() = default;

const char *Ext2Filesystem::name() const {
    return "ext2";
}

File *Ext2Filesystem::mount(File *mount_point, BlockDevice *device) {
    if (!mount_point || !device)
        return nullptr;

    Ext2Mount *mount = new Ext2Mount(device);
    if (!mount)
        return nullptr;

    if (!mount->initialize()) {
        delete mount;
        return nullptr;
    }

    File *parent = mount_point->getParent();
    char *name = mount_point->getName();

    Ext2Directory *root = new Ext2Directory(name, mount, EXT2_ROOT_INO, true);

    if (parent) {
        // Remove the old mount point and replace it with the ext2 root
        delete mount_point;
        parent->addChild(root);
    } else {
        // mounting root filesystem
        delete mount_point;
        root->setParent(nullptr);
    }

    return root;
}

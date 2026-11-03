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
static constexpr u32 EXT2_FILETYPE = 2;
static constexpr u16 EXT2_MODE_TYPE = 0xf000;
static constexpr u16 EXT2_MODE_FILE = 0x8000;
static constexpr u16 EXT2_MODE_DIR = 0x4000;

class Ext2Mount {
public:
    explicit Ext2Mount(BlockDevice *device)
        : device_(device), groups_(nullptr), group_count_(0),
          block_size_(1024), inode_size_(128), ptrs_per_block_(256), references_(0) {}

    ~Ext2Mount() { kfree(groups_); }
    void retain() { ++references_; }
    void release() { if (!--references_) delete this; }

    bool initialize() {
        if (!device_ || device_->read(1024, (u8 *)&super_, sizeof(super_)) != RETURN_OK)
            return false;
        if (super_.s_magic != EXT2_SUPER_MAGIC || super_.s_rev_level > 1 ||
            super_.s_log_block_size > 2 || (super_.s_feature_incompat & ~EXT2_FILETYPE))
            return false;

        block_size_ = 1024u << super_.s_log_block_size;
        inode_size_ = super_.s_rev_level ? super_.s_inode_size : 128;
        ptrs_per_block_ = block_size_ / sizeof(u32);
        if (super_.s_first_data_block != (block_size_ == 1024 ? 1U : 0U) ||
            super_.s_blocks_count <= super_.s_first_data_block ||
            super_.s_blocks_count > 0xffffffffU / block_size_ ||
            !super_.s_blocks_per_group || super_.s_blocks_per_group > block_size_ * 8 ||
            !super_.s_inodes_per_group || super_.s_inodes_per_group > block_size_ * 8 ||
            super_.s_inodes_count < EXT2_ROOT_INO || inode_size_ < sizeof(ext2_inode) ||
            inode_size_ > block_size_ || (inode_size_ & (inode_size_ - 1)))
            return false;

        group_count_ = 1 + (super_.s_blocks_count - super_.s_first_data_block - 1) /
            super_.s_blocks_per_group;
        if ((super_.s_inodes_count - 1) / super_.s_inodes_per_group >= group_count_ ||
            group_count_ > 0xffffffffU / sizeof(ext2_group_desc)) return false;
        u32 table_size = group_count_ * sizeof(ext2_group_desc);
        u32 first_gdt_block = block_size_ == 1024 ? 2 : 1;
        u32 table_blocks = 1 + (table_size - 1) / block_size_;
        if (first_gdt_block >= super_.s_blocks_count ||
            table_blocks > super_.s_blocks_count - first_gdt_block) return false;
        groups_ = (ext2_group_desc *)kmalloc(table_size);
        if (!groups_ || device_->read(first_gdt_block * block_size_, (u8 *)groups_, table_size) != RETURN_OK)
            return false;

        u32 inode_blocks = (super_.s_inodes_per_group * inode_size_ + block_size_ - 1) / block_size_;
        for (u32 i = 0; i < group_count_; ++i) {
            u32 start = super_.s_first_data_block + i * super_.s_blocks_per_group;
            u32 count = super_.s_blocks_count - start;
            if (count > super_.s_blocks_per_group) count = super_.s_blocks_per_group;
            const ext2_group_desc &group = groups_[i];
            if (group.bg_inode_table < start || group.bg_inode_table >= start + count ||
                inode_blocks > start + count - group.bg_inode_table ||
                group.bg_block_bitmap < start || group.bg_block_bitmap >= start + count ||
                group.bg_inode_bitmap < start || group.bg_inode_bitmap >= start + count ||
                !group.bg_block_bitmap || !group.bg_inode_bitmap || !group.bg_inode_table ||
                group.bg_block_bitmap == group.bg_inode_bitmap ||
                (group.bg_block_bitmap >= group.bg_inode_table &&
                 group.bg_block_bitmap - group.bg_inode_table < inode_blocks) ||
                (group.bg_inode_bitmap >= group.bg_inode_table &&
                 group.bg_inode_bitmap - group.bg_inode_table < inode_blocks)) return false;
        }
        return true;
    }

    bool read_inode(u32 ino, ext2_inode *out) {
        if (!out || !ino || ino > super_.s_inodes_count) return false;
        u32 index = ino - 1;
        u32 group = index / super_.s_inodes_per_group;
        u32 offset = (index % super_.s_inodes_per_group) * inode_size_;
        u64 position = (u64)groups_[group].bg_inode_table * block_size_ + offset;
        if (position + sizeof(*out) > (u64)super_.s_blocks_count * block_size_ ||
            device_->read((u32)position, (u8 *)out, sizeof(*out)) != RETURN_OK) return false;
        return !(out->i_flags & 0x80004U) &&
            ((out->i_mode & EXT2_MODE_TYPE) != EXT2_MODE_FILE || !out->i_dir_acl);
    }

    bool read_block(u32 block, void *buffer) {
        return block && block < super_.s_blocks_count &&
            device_->read(block * block_size_, (u8 *)buffer, block_size_) == RETURN_OK;
    }

    bool get_block(const ext2_inode &inode, u32 index, u32 &block) {
        if (index < 12) block = inode.i_block[index];
        else {
            index -= 12;
            if (index >= ptrs_per_block_) return false;
            if (!inode.i_block[12]) { block = 0; return true; }
            u32 *entries = (u32 *)kmalloc(block_size_);
            if (!entries) return false;
            bool valid = read_block(inode.i_block[12], entries);
            if (valid) block = entries[index];
            kfree(entries);
            if (!valid) return false;
        }
        return block < super_.s_blocks_count;
    }

    u32 read_file_data(const ext2_inode &inode, u32 offset, u8 *buffer, u32 size) {
        if (!size || offset >= inode.i_size) return 0;
        if (!buffer) return (u32)-22;
        if (size > inode.i_size - offset) size = inode.i_size - offset;
        u8 *block_buffer = (u8 *)kmalloc(block_size_);
        if (!block_buffer) return (u32)-12;
        u32 total = 0;
        while (size) {
            u32 block;
            if (!get_block(inode, offset / block_size_, block) ||
                (block && !read_block(block, block_buffer))) break;
            if (!block) memset(block_buffer, 0, block_size_);
            u32 within = offset % block_size_;
            u32 count = block_size_ - within;
            if (count > size) count = size;
            memcpy(buffer + total, block_buffer + within, count);
            total += count;
            offset += count;
            size -= count;
        }
        kfree(block_buffer);
        return total ? total : (u32)-5;
    }

    bool load_directory(Ext2Directory *dir) {
        ext2_inode inode;
        if (!dir || !read_inode(dir->inode(), &inode) ||
            (inode.i_mode & EXT2_MODE_TYPE) != EXT2_MODE_DIR || !inode.i_size ||
            inode.i_size % block_size_ || inode.i_size / block_size_ > 12 + ptrs_per_block_)
            return false;
        u8 *buffer = (u8 *)kmalloc(block_size_);
        if (!buffer) return false;
        bool valid = true;
        for (u32 i = 0; valid && i < inode.i_size / block_size_; ++i) {
            u32 block;
            if (!get_block(inode, i, block) || !read_block(block, buffer)) { valid = false; break; }
            for (u32 offset = 0; offset < block_size_;) {
                if (block_size_ - offset < sizeof(ext2_dir_entry)) { valid = false; break; }
                ext2_dir_entry *entry = (ext2_dir_entry *)(buffer + offset);
                if (entry->rec_len < sizeof(ext2_dir_entry) || (entry->rec_len & 3) ||
                    entry->rec_len > block_size_ - offset ||
                    entry->name_len > entry->rec_len - sizeof(ext2_dir_entry) ||
                    entry->inode > super_.s_inodes_count ||
                    (!(super_.s_feature_incompat & EXT2_FILETYPE) && entry->file_type)) {
                    valid = false;
                    break;
                }
                if (entry->inode) {
                    if (!entry->name_len) { valid = false; break; }
                    char name[256];
                    for (u32 j = 0; j < entry->name_len; ++j) {
                        name[j] = entry->name[j];
                        if (!name[j] || name[j] == '/') valid = false;
                    }
                    if (!valid) break;
                    name[entry->name_len] = 0;
                    if (strcmp(name, ".") && strcmp(name, "..")) {
                        ext2_inode child_inode;
                        if (!read_inode(entry->inode, &child_inode)) { valid = false; break; }
                        u16 type = child_inode.i_mode & EXT2_MODE_TYPE;
                        u8 directory_type = type == EXT2_MODE_DIR ? 2 : (type == EXT2_MODE_FILE ? 1 : 0);
                        if (entry->file_type && directory_type && entry->file_type != directory_type) {
                            valid = false;
                            break;
                        }
                        File *child = nullptr;
                        if (type == EXT2_MODE_DIR)
                            child = new Ext2Directory(name, dir->mount(), entry->inode);
                        else if (type == EXT2_MODE_FILE)
                            child = new Ext2RegularFile(name, dir->mount(), entry->inode, child_inode.i_size);
                        if (directory_type && (!child || dir->addChild(child) != RETURN_OK)) {
                            delete child;
                            valid = false;
                            break;
                        }
                    }
                }
                offset += entry->rec_len;
            }
        }
        kfree(buffer);
        if (!valid) {
            while (dir->getChild()) dir->getChild()->remove();
            return false;
        }
        dir->setSize(inode.i_size);
        return true;
    }

private:
    BlockDevice *device_;
    ext2_super_block super_;
    ext2_group_desc *groups_;
    u32 group_count_;
    u32 block_size_;
    u32 inode_size_;
    u32 ptrs_per_block_;
    u32 references_;
};

Ext2Filesystem ext2_driver;

Ext2Node::Ext2Node(const char *name, u8 type, Ext2Mount *mount, u32 inode)
    : File(name, type), mount_(mount), inode_(inode) {
    if (mount_) mount_->retain();
}

Ext2Node::~Ext2Node() { if (mount_) mount_->release(); }

Ext2Directory::Ext2Directory(const char *name, Ext2Mount *mount, u32 inode)
    : Ext2Node(name, TYPE_DIRECTORY, mount, inode), loaded_(false) {}

Ext2Directory::~Ext2Directory() = default;

bool Ext2Directory::load() {
    if (!loaded_ && mount_) loaded_ = mount_->load_directory(this);
    return loaded_;
}

void Ext2Directory::scan() { load(); }

Ext2RegularFile::Ext2RegularFile(const char *name, Ext2Mount *mount, u32 inode, u32 size)
    : Ext2Node(name, TYPE_FILE, mount, inode) { setSize(size); }

u32 Ext2RegularFile::read(u32 pos, u8 *buffer, u32 size) {
    ext2_inode inode;
    if (!mount_ || !mount_->read_inode(inode_, &inode)) return (u32)-5;
    return mount_->read_file_data(inode, pos, buffer, size);
}

Ext2Filesystem::Ext2Filesystem() = default;
const char *Ext2Filesystem::name() const { return "ext2"; }

File *Ext2Filesystem::mount(File *mount_point, BlockDevice *device) {
    if (!mount_point || !device) return nullptr;
    Ext2Mount *mount = new Ext2Mount(device);
    if (!mount || !mount->initialize()) { delete mount; return nullptr; }
    Ext2Directory *root = new Ext2Directory(mount_point->getName(), mount, EXT2_ROOT_INO);
    if (!root) { delete mount; return nullptr; }
    if (!root->getName() || !root->load()) { delete root; return nullptr; }
    File *parent = mount_point->getParent();
    mount_point->remove();
    if (parent) parent->addChild(root);
    return root;
}

#include <os.h>
#include <core/ext2.h>
#include <core/block_device.h>
#include <runtime/alloc.h>
#include <fcntl.h>

extern "C" {
    void *memcpy(void *dest, const void *src, int n);
    void *memset(void *s, int c, int n);
    int strcmp(const char *s1, const char *s2);
}

static_assert(sizeof(ext2_super_block) == 1024, "ext2 superblock size");
static_assert(sizeof(ext2_inode) == 128, "ext2 inode size");
static_assert(sizeof(ext2_group_desc) == 32, "ext2 descriptor size");

static constexpr u16 EXT2_SUPER_MAGIC = 0xEF53;
static constexpr u32 EXT2_ROOT_INO = 2;
static constexpr u32 EXT2_FILETYPE = 2;
static constexpr u32 EXT2_SPARSE_SUPER = 1;
static constexpr u32 EXT2_LARGE_FILE = 2;
static constexpr u32 EXT2_EXT_ATTR = 8;
static constexpr u32 EXT2_RESIZE_INODE = 16;
static constexpr u32 EXT2_DIR_INDEX = 32;
static constexpr u32 EXT2_IMMUTABLE = 0x10;
static constexpr u32 EXT2_APPEND = 0x20;
static constexpr u16 EXT2_MODE_TYPE = 0xf000;
static constexpr u16 EXT2_MODE_FILE = 0x8000;
static constexpr u16 EXT2_MODE_DIR = 0x4000;

class Ext2Transaction;

class Ext2Mount {
    friend class Ext2Transaction;
public:
    Ext2Mount(BlockDevice *device, bool writable)
        : device_(device), groups_(nullptr), group_count_(0),
          block_size_(1024), inode_size_(128), ptrs_per_block_(256), references_(0),
          writable_(writable), nodes_(nullptr) {}

    ~Ext2Mount() { kfree(groups_); }
    void retain() { ++references_; }
    void release() { if (!--references_) delete this; }

    bool initialize() {
        if (!device_ || device_->read(1024, (u8 *)&super_, sizeof(super_)) != RETURN_OK)
            return false;
        if (super_.s_magic != EXT2_SUPER_MAGIC || super_.s_rev_level > 1 ||
            super_.s_log_block_size > 2 || (super_.s_feature_incompat & ~EXT2_FILETYPE))
            return false;

        if (writable_ && (super_.s_state != 1 || super_.s_last_orphan ||
            (super_.s_rev_level && (super_.s_first_ino < 11 ||
                                   super_.s_first_ino > super_.s_inodes_count)) ||
            (super_.s_feature_compat & ~(EXT2_EXT_ATTR | EXT2_RESIZE_INODE | EXT2_DIR_INDEX)) ||
            (super_.s_feature_ro_compat & ~(EXT2_SPARSE_SUPER | EXT2_LARGE_FILE)))) return false;

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

        u8 end;
        if (device_->read(super_.s_blocks_count * block_size_ - 1, &end, 1) != RETURN_OK)
            return false;
        group_count_ = 1 + (super_.s_blocks_count - super_.s_first_data_block - 1) /
            super_.s_blocks_per_group;
        if ((super_.s_inodes_count - 1) / super_.s_inodes_per_group >= group_count_ ||
            group_count_ > 0xffffffffU / sizeof(ext2_group_desc)) return false;
        if (writable_ && (super_.s_inodes_count - 1) / super_.s_inodes_per_group + 1 != group_count_)
            return false;
        u32 table_size = group_count_ * sizeof(ext2_group_desc);
        u32 first_gdt_block = block_size_ == 1024 ? 2 : 1;
        table_blocks_ = 1 + (table_size - 1) / block_size_;
        if (first_gdt_block >= super_.s_blocks_count ||
            table_blocks_ > super_.s_blocks_count - first_gdt_block) return false;
        groups_ = (ext2_group_desc *)kmalloc(table_size);
        if (!groups_ || device_->read(first_gdt_block * block_size_, (u8 *)groups_, table_size) != RETURN_OK)
            return false;

        inode_blocks_ = (super_.s_inodes_per_group * inode_size_ + block_size_ - 1) / block_size_;
        u64 free_blocks = 0, free_inodes = 0;
        for (u32 i = 0; i < group_count_; ++i) {
            u32 start = super_.s_first_data_block + i * super_.s_blocks_per_group;
            u32 count = super_.s_blocks_count - start;
            if (count > super_.s_blocks_per_group) count = super_.s_blocks_per_group;
            const ext2_group_desc &group = groups_[i];
            u32 inodes = super_.s_inodes_count > i * super_.s_inodes_per_group
                ? super_.s_inodes_count - i * super_.s_inodes_per_group : 0;
            if (inodes > super_.s_inodes_per_group) inodes = super_.s_inodes_per_group;
            if (writable_ && (group.bg_free_blocks_count > count ||
                             group.bg_free_inodes_count > inodes)) return false;
            free_blocks += group.bg_free_blocks_count;
            free_inodes += group.bg_free_inodes_count;
            if (writable_ && has_superblock(i)) {
                u32 reserved = reserved_blocks();
                if (reserved > count || group.bg_block_bitmap < start + reserved ||
                    group.bg_inode_bitmap < start + reserved ||
                    group.bg_inode_table < start + reserved) return false;
            }
            if (group.bg_inode_table < start || group.bg_inode_table >= start + count ||
                inode_blocks_ > start + count - group.bg_inode_table ||
                group.bg_block_bitmap < start || group.bg_block_bitmap >= start + count ||
                group.bg_inode_bitmap < start || group.bg_inode_bitmap >= start + count ||
                !group.bg_block_bitmap || !group.bg_inode_bitmap || !group.bg_inode_table ||
                group.bg_block_bitmap == group.bg_inode_bitmap ||
                (group.bg_block_bitmap >= group.bg_inode_table &&
                 group.bg_block_bitmap - group.bg_inode_table < inode_blocks_) ||
                (group.bg_inode_bitmap >= group.bg_inode_table &&
                 group.bg_inode_bitmap - group.bg_inode_table < inode_blocks_)) return false;
        }
        return !writable_ || (free_blocks == super_.s_free_blocks_count &&
                              free_inodes == super_.s_free_inodes_count);
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

    int check_write(u32 number, const ext2_inode &inode, bool permissions = true) {
        if (!writable_) return -30;
        if (number < (super_.s_rev_level ? super_.s_first_ino : 11) ||
            (inode.i_mode & EXT2_MODE_TYPE) != EXT2_MODE_FILE ||
            (permissions && !(inode.i_mode & 0222)) || (inode.i_flags & (EXT2_IMMUTABLE | EXT2_APPEND))) return -13;
        return 0;
    }

    u32 write_file_data(u32 number, const ext2_inode &inode, u32 offset, u8 *buffer, u32 size);
    int create_node(Ext2Directory *dir, const char *name, u32 mode, bool directory, File *&file);
    int truncate_file(u32 number, u32 size);
    int remove_node(Ext2Directory *dir, const char *name, bool directory);
    int empty_directory(Ext2Transaction &transaction, const ext2_inode &inode, u32 number, u32 parent);
    int reap_inode(u32 number, Ext2Node *closing = nullptr);

    void attach_node(Ext2Node *file) {
        IRQGuard guard;
        file->next_node_ = nodes_;
        nodes_ = file;
    }

    void detach_node(Ext2Node *file) {
        IRQGuard guard;
        Ext2Node **link = &nodes_;
        while (*link && *link != file) link = &(*link)->next_node_;
        if (*link) *link = file->next_node_;
    }

    bool inode_referenced(u32 number, Ext2Node *closing = nullptr) {
        for (Ext2Node *file = nodes_; file; file = file->next_node_) {
            u32 count = file->references;
            if (file == closing && count) --count;
            if (file->inode() == number && count) return true;
        }
        return false;
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
    bool has_superblock(u32 group) {
        if (!(super_.s_feature_ro_compat & EXT2_SPARSE_SUPER) || group < 2) return true;
        for (u32 base : sparse_super_bases) {
            u32 value = group;
            while (value % base == 0) value /= base;
            if (value == 1) return true;
        }
        return false;
    }

    u32 reserved_blocks() const {
        return 1 + table_blocks_ + ((super_.s_feature_compat & EXT2_RESIZE_INODE)
            ? super_.s_reserved_gdt_blocks : 0);
    }

    bool data_block(u32 block) {
        if (!block || block < super_.s_first_data_block || block >= super_.s_blocks_count)
            return false;
        u32 relative = block - super_.s_first_data_block;
        u32 group = relative / super_.s_blocks_per_group;
        u32 bit = relative % super_.s_blocks_per_group;
        const ext2_group_desc &desc = groups_[group];
        if ((has_superblock(group) && bit < reserved_blocks()) || block == desc.bg_block_bitmap ||
            block == desc.bg_inode_bitmap ||
            (block >= desc.bg_inode_table && block - desc.bg_inode_table < inode_blocks_))
            return false;
        return true;
    }

    static constexpr u32 sparse_super_bases[] = {3, 5, 7};
    BlockDevice *device_;
    ext2_super_block super_;
    ext2_group_desc *groups_;
    u32 group_count_;
    u32 block_size_;
    u32 inode_size_;
    u32 ptrs_per_block_;
    u32 references_;
    u32 table_blocks_;
    u32 inode_blocks_;
    bool writable_;
    Ext2Node *nodes_;
};

class Ext2Transaction {
public:
    enum Access { Read, Write, Initialize };
    explicit Ext2Transaction(Ext2Mount &mount)
        : mount_(mount), super_(mount.super_), changes_(nullptr), error_(0) {}

    ~Ext2Transaction() {
        while (changes_) {
            Change *next = changes_->next;
            kfree(changes_);
            changes_ = next;
        }
    }

    int error() const { return error_; }
    void fail(int error) { if (!error_) error_ = error; }

    u8 *block(u32 number, Access access = Read) {
        if (error_) return nullptr;
        if (number >= super_.s_blocks_count) { fail(-5); return nullptr; }
        for (Change *change = changes_; change; change = change->next) {
            if (change->number == number) {
                change->dirty |= access != Read;
                if (access == Initialize) {
                    change->initialized = true;
                    memset(change + 1, 0, mount_.block_size_);
                }
                return (u8 *)(change + 1);
            }
        }
        Change *change = (Change *)kmalloc(sizeof(Change) + mount_.block_size_);
        if (!change) { fail(-12); return nullptr; }
        u8 *data = (u8 *)(change + 1);
        if (mount_.device_->read(number * mount_.block_size_, data, mount_.block_size_) != RETURN_OK) {
            kfree(change);
            fail(-5);
            return nullptr;
        }
        *change = {number, access != Read, access == Initialize, changes_};
        if (access == Initialize) memset(data, 0, mount_.block_size_);
        changes_ = change;
        return data;
    }

    bool allocated_block(u32 number) {
        if (!mount_.data_block(number)) { fail(-5); return false; }
        u32 relative = number - super_.s_first_data_block;
        u32 group = relative / super_.s_blocks_per_group;
        u32 bit = relative % super_.s_blocks_per_group;
        u8 *bitmap = block(mount_.groups_[group].bg_block_bitmap);
        if (!bitmap) return false;
        if (!(bitmap[bit / 8] & (1U << (bit % 8)))) { fail(-5); return false; }
        return true;
    }

    bool allocated_inode(u32 number) {
        if (!number || number > super_.s_inodes_count) { fail(-5); return false; }
        u32 group = (number - 1) / super_.s_inodes_per_group;
        u32 bit = (number - 1) % super_.s_inodes_per_group;
        u8 *bitmap = block(mount_.groups_[group].bg_inode_bitmap);
        if (!bitmap) return false;
        if (!(bitmap[bit / 8] & (1U << (bit % 8)))) { fail(-5); return false; }
        return true;
    }

    u32 allocate_block(u32 preferred_group) {
        if (error_) return 0;
        if (!super_.s_free_blocks_count) { fail(-28); return 0; }
        for (u32 i = 0; i < mount_.group_count_; ++i) {
            u32 group = (preferred_group + i) % mount_.group_count_;
            ext2_group_desc *desc = descriptor(group);
            if (!desc) return 0;
            if (!desc->bg_free_blocks_count) continue;
            u32 start = super_.s_first_data_block + group * super_.s_blocks_per_group;
            u32 count = super_.s_blocks_count - start;
            if (count > super_.s_blocks_per_group) count = super_.s_blocks_per_group;
            u8 *bitmap = block(desc->bg_block_bitmap);
            if (!bitmap) return 0;
            for (u32 bit = 0; bit < count; ++bit) {
                if (bitmap[bit / 8] & (1U << (bit % 8))) continue;
                u32 number = start + bit;
                if (!mount_.data_block(number)) { fail(-5); return 0; }
                if (!block(desc->bg_block_bitmap, Write) || !descriptor(group, true)) return 0;
                bitmap[bit / 8] |= 1U << (bit % 8);
                --desc->bg_free_blocks_count;
                --super_.s_free_blocks_count;
                if (!block(number, Initialize)) return 0;
                return number;
            }
            fail(-5);
            return 0;
        }
        fail(-28);
        return 0;
    }

    u32 allocate_inode(u32 preferred_group, bool directory = false) {
        if (error_) return 0;
        if (!super_.s_free_inodes_count) { fail(-28); return 0; }
        for (u32 i = 0; i < mount_.group_count_; ++i) {
            u32 group = (preferred_group + i) % mount_.group_count_;
            ext2_group_desc *desc = descriptor(group);
            if (!desc) return 0;
            if (!desc->bg_free_inodes_count) continue;
            u32 start = group * super_.s_inodes_per_group;
            u32 count = start < super_.s_inodes_count ? super_.s_inodes_count - start : 0;
            if (count > super_.s_inodes_per_group) count = super_.s_inodes_per_group;
            u8 *bitmap = block(desc->bg_inode_bitmap);
            if (!bitmap) return 0;
            for (u32 bit = 0; bit < count; ++bit) {
                if (bitmap[bit / 8] & (1U << (bit % 8))) continue;
                u32 number = start + bit + 1;
                if (number < (super_.s_rev_level ? super_.s_first_ino : 11)) { fail(-5); return 0; }
                if (!block(desc->bg_inode_bitmap, Write) || !descriptor(group, true)) return 0;
                bitmap[bit / 8] |= 1U << (bit % 8);
                if (directory && desc->bg_used_dirs_count >= count) { fail(-5); return 0; }
                --desc->bg_free_inodes_count;
                --super_.s_free_inodes_count;
                if (directory) ++desc->bg_used_dirs_count;
                u8 *data = inode_slot(number);
                if (!data) return 0;
                memset(data, 0, mount_.inode_size_);
                return number;
            }
            fail(-5);
            return 0;
        }
        fail(-28);
        return 0;
    }

    void store_inode(u32 number, const ext2_inode &inode) {
        u8 *slot = inode_slot(number);
        if (slot) memcpy(slot, &inode, sizeof(inode));
    }

    u32 file_block(ext2_inode &inode, u32 index, u32 preferred_group) {
        if (index >= 12 + mount_.ptrs_per_block_) { fail(-27); return 0; }
        u32 sectors = mount_.block_size_ / 512;
        if (inode.i_blocks > 0xffffffffU - 2 * sectors) { fail(-5); return 0; }
        u32 *entries = nullptr;
        if (inode.i_block[12]) {
            if (!allocated_block(inode.i_block[12]) || inode.i_block[12] == inode.i_file_acl) {
                fail(-5);
                return 0;
            }
            for (u32 i = 0; i < 12; ++i)
                if (inode.i_block[i] == inode.i_block[12]) { fail(-5); return 0; }
            entries = (u32 *)block(inode.i_block[12]);
            if (!entries) return 0;
        }
        if (index >= 12 && !entries) {
            inode.i_block[12] = allocate_block(preferred_group);
            if (!inode.i_block[12]) return 0;
            inode.i_blocks += sectors;
            entries = (u32 *)block(inode.i_block[12]);
            if (!entries) return 0;
        }
        u32 number = index < 12 ? inode.i_block[index] : entries[index - 12];
        if (!number) {
            number = allocate_block(preferred_group);
            if (!number) return 0;
            if (index < 12) inode.i_block[index] = number;
            else {
                if (!block(inode.i_block[12], Write)) return 0;
                entries[index - 12] = number;
            }
            inode.i_blocks += sectors;
        }
        if (!allocated_block(number) || number == inode.i_file_acl || number == inode.i_block[12] ||
            number == inode.i_block[13] || number == inode.i_block[14]) {
            fail(-5);
            return 0;
        }
        for (u32 i = 0; i < 12; ++i)
            if (i != index && inode.i_block[i] == number) { fail(-5); return 0; }
        if (entries) {
            for (u32 i = 0; i < mount_.ptrs_per_block_; ++i)
                if (i + 12 != index && entries[i] == number) { fail(-5); return 0; }
        }
        return number;
    }

    void free_block(u32 number) {
        if (!allocated_block(number)) return;
        u32 relative = number - super_.s_first_data_block;
        u32 group = relative / super_.s_blocks_per_group;
        u32 bit = relative % super_.s_blocks_per_group;
        ext2_group_desc *desc = descriptor(group, true);
        if (!desc) return;
        u32 start = super_.s_first_data_block + group * super_.s_blocks_per_group;
        u32 count = super_.s_blocks_count - start;
        if (count > super_.s_blocks_per_group) count = super_.s_blocks_per_group;
        if (desc->bg_free_blocks_count >= count ||
            super_.s_free_blocks_count >= super_.s_blocks_count - super_.s_first_data_block) {
            fail(-5);
            return;
        }
        u8 *bitmap = block(desc->bg_block_bitmap, Write);
        if (!bitmap) return;
        bitmap[bit / 8] &= ~(1U << (bit % 8));
        ++desc->bg_free_blocks_count;
        ++super_.s_free_blocks_count;
        for (Change *change = changes_; change; change = change->next)
            if (change->number == number) change->dirty = false;
    }

    void truncate_inode(ext2_inode &inode, u32 size) {
        u32 count = 12 + mount_.ptrs_per_block_;
        if (size > count * mount_.block_size_) { fail(-27); return; }
        if (inode.i_block[13] || inode.i_block[14] || inode.i_faddr) { fail(-95); return; }
        u32 *entries = nullptr;
        u32 allocated = 0;
        if (inode.i_file_acl) {
            if (!allocated_block(inode.i_file_acl)) return;
            ++allocated;
        }
        if (inode.i_block[12]) {
            if (inode.i_block[12] == inode.i_file_acl || !allocated_block(inode.i_block[12])) {
                fail(-5);
                return;
            }
            entries = (u32 *)block(inode.i_block[12]);
            if (!entries) return;
            ++allocated;
        }

        u32 blocks[12 + 1024];
        for (u32 i = 0; i < count; ++i) {
            u32 number = i < 12 ? inode.i_block[i] : (entries ? entries[i - 12] : 0);
            blocks[i] = number;
            if (!number) continue;
            if (number == inode.i_block[12] || number == inode.i_file_acl || !allocated_block(number)) {
                fail(-5);
                return;
            }
            for (u32 j = 0; j < i; ++j)
                if (blocks[j] == number) { fail(-5); return; }
            ++allocated;
        }
        u32 sectors = mount_.block_size_ / 512;
        if (inode.i_blocks != allocated * sectors) { fail(-5); return; }

        u32 keep = (size + mount_.block_size_ - 1) / mount_.block_size_;
        bool keep_indirect = false;
        for (u32 i = 0; i < count; ++i) {
            if (!blocks[i]) continue;
            if (i < keep) {
                if (i >= 12) keep_indirect = true;
                continue;
            }
            free_block(blocks[i]);
            if (error_) return;
            inode.i_blocks -= sectors;
            if (i < 12) inode.i_block[i] = 0;
            else {
                if (!block(inode.i_block[12], Write)) return;
                entries[i - 12] = 0;
            }
        }
        if (inode.i_block[12] && !keep_indirect) {
            free_block(inode.i_block[12]);
            if (error_) return;
            inode.i_block[12] = 0;
            inode.i_blocks -= sectors;
        }

        u32 begin = size < inode.i_size ? size : inode.i_size;
        u32 end = size < inode.i_size
            ? ((size + mount_.block_size_ - 1) / mount_.block_size_) * mount_.block_size_ : size;
        for (u32 offset = begin; offset < end;) {
            u32 index = offset / mount_.block_size_;
            u32 within = offset % mount_.block_size_;
            u32 bytes = mount_.block_size_ - within;
            if (bytes > end - offset) bytes = end - offset;
            if (index < keep && blocks[index]) {
                u8 *data = block(blocks[index], Write);
                if (!data) return;
                memset(data + within, 0, bytes);
            }
            offset += bytes;
        }
        inode.i_size = size;
    }

    void orphan_inode(u32 number, ext2_inode &inode) {
        inode.i_dtime = super_.s_last_orphan;
        super_.s_last_orphan = number;
    }

    bool remove_orphan(u32 number, ext2_inode &inode) {
        u32 previous = 0;
        u32 current = super_.s_last_orphan;
        for (u32 seen = 0; current && seen < super_.s_inodes_count; ++seen) {
            ext2_inode entry;
            if (!allocated_inode(current) || !mount_.read_inode(current, &entry) || entry.i_links_count) {
                fail(-5);
                return false;
            }
            if (current == number) {
                if (previous) {
                    ext2_inode parent;
                    if (!mount_.read_inode(previous, &parent)) { fail(-5); return false; }
                    parent.i_dtime = entry.i_dtime;
                    store_inode(previous, parent);
                } else {
                    super_.s_last_orphan = entry.i_dtime;
                }
                inode = entry;
                return true;
            }
            previous = current;
            current = entry.i_dtime;
        }
        if (current) fail(-5);
        return false;
    }

    void release_inode(u32 number, ext2_inode &inode) {
        truncate_inode(inode, 0);
        if (error_) return;
        if (inode.i_file_acl) {
            u32 *header = (u32 *)block(inode.i_file_acl);
            if (!header) return;
            if (header[0] != 0xea020000 || !header[1] || header[1] > 1024 || header[2] != 1) {
                fail(-5);
                return;
            }
            if (header[1] == 1) free_block(inode.i_file_acl);
            else {
                if (!block(inode.i_file_acl, Write)) return;
                --header[1];
            }
        }
        if (!allocated_inode(number)) return;
        u32 group = (number - 1) / super_.s_inodes_per_group;
        u32 bit = (number - 1) % super_.s_inodes_per_group;
        ext2_group_desc *desc = descriptor(group, true);
        if (!desc) return;
        u32 count = super_.s_inodes_count - group * super_.s_inodes_per_group;
        if (count > super_.s_inodes_per_group) count = super_.s_inodes_per_group;
        if (desc->bg_free_inodes_count >= count || super_.s_free_inodes_count >= super_.s_inodes_count) {
            fail(-5);
            return;
        }
        u8 *bitmap = block(desc->bg_inode_bitmap, Write);
        u8 *data = inode_slot(number);
        if (!bitmap || !data) return;
        if ((inode.i_mode & EXT2_MODE_TYPE) == EXT2_MODE_DIR) {
            if (!desc->bg_used_dirs_count) { fail(-5); return; }
            --desc->bg_used_dirs_count;
        }
        bitmap[bit / 8] &= ~(1U << (bit % 8));
        ++desc->bg_free_inodes_count;
        ++super_.s_free_inodes_count;
        memset(data, 0, mount_.inode_size_);
    }

    int commit() {
        if (error_) return error_;
        u16 dirty = 0;
        if (mount_.device_->write(1024 + __builtin_offsetof(ext2_super_block, s_state),
                                 (u8 *)&dirty, sizeof(dirty)) != RETURN_OK)
            return io_failure();
        for (u32 pass = 0; pass < 2; ++pass) {
            for (Change *change = changes_; change; change = change->next) {
                if (change->dirty && change->initialized == (pass == 0) &&
                    mount_.device_->write(change->number * mount_.block_size_,
                        (u8 *)(change + 1), mount_.block_size_) != RETURN_OK) return io_failure();
            }
        }
        super_.s_state = 0;
        if (mount_.device_->write(1024, (u8 *)&super_, sizeof(super_)) != RETURN_OK)
            return io_failure();
        u16 clean = super_.s_last_orphan ? 0 : 1;
        if (mount_.device_->write(1024 + __builtin_offsetof(ext2_super_block, s_state),
                                 (u8 *)&clean, sizeof(clean)) != RETURN_OK)
            return io_failure();
        super_.s_state = clean;
        mount_.super_ = super_;

        u32 first = mount_.block_size_ == 1024 ? 2 : 1;
        u32 bytes = mount_.group_count_ * sizeof(ext2_group_desc);
        for (Change *change = changes_; change; change = change->next) {
            if (!change->dirty || change->number < first ||
                change->number - first >= mount_.table_blocks_) continue;
            u32 offset = (change->number - first) * mount_.block_size_;
            u32 count = bytes - offset;
            if (count > mount_.block_size_) count = mount_.block_size_;
            memcpy((u8 *)mount_.groups_ + offset, change + 1, count);
        }
        return 0;
    }

private:
    struct Change {
        u32 number;
        bool dirty;
        bool initialized;
        Change *next;
    };

    ext2_group_desc *descriptor(u32 group, bool dirty = false) {
        u32 offset = group * sizeof(ext2_group_desc);
        u8 *data = block((mount_.block_size_ == 1024 ? 2 : 1) + offset / mount_.block_size_, dirty ? Write : Read);
        return data ? (ext2_group_desc *)(data + offset % mount_.block_size_) : nullptr;
    }

    u8 *inode_slot(u32 number) {
        if (!number || number > super_.s_inodes_count) { fail(-5); return nullptr; }
        u32 group = (number - 1) / super_.s_inodes_per_group;
        u32 offset = (number - 1) % super_.s_inodes_per_group * mount_.inode_size_;
        u8 *data = block(mount_.groups_[group].bg_inode_table + offset / mount_.block_size_, Write);
        return data ? data + offset % mount_.block_size_ : nullptr;
    }

    int io_failure() {
        mount_.super_.s_state = 0;
        mount_.writable_ = false;
        return -5;
    }

    Ext2Mount &mount_;
    ext2_super_block super_;
    Change *changes_;
    int error_;
};

u32 Ext2Mount::write_file_data(u32 number, const ext2_inode &original, u32 offset, u8 *buffer, u32 size) {
    IRQGuard guard;
    int error = check_write(number, original, false);
    if (error) return error;
    if (!size) return 0;
    if (!buffer) return (u32)-22;
    u32 limit = (12 + ptrs_per_block_) * block_size_;
    if (original.i_size > limit || offset >= limit || size > limit - offset) return (u32)-27;

    Ext2Transaction transaction(*this);
    if (!transaction.allocated_inode(number)) return transaction.error();
    ext2_inode inode = original;
    u32 group = (number - 1) / super_.s_inodes_per_group;
    if (offset > inode.i_size && inode.i_size % block_size_) {
        u32 last = inode.i_size / block_size_;
        u32 block;
        if (!get_block(inode, last, block)) return (u32)-5;
        if (block) {
            if (block == inode.i_file_acl || block == inode.i_block[12] ||
                block == inode.i_block[13] || block == inode.i_block[14]) return (u32)-5;
            if (!transaction.allocated_block(block)) return transaction.error();
            u8 *data = transaction.block(block, Ext2Transaction::Write);
            if (!data) return transaction.error();
            u32 end = (offset / block_size_ == last) ? offset % block_size_ : block_size_;
            memset(data + inode.i_size % block_size_, 0, end - inode.i_size % block_size_);
        }
    }

    for (u32 copied = 0; copied < size;) {
        u32 position = offset + copied;
        u32 block = transaction.file_block(inode, position / block_size_, group);
        if (!block) return transaction.error();
        u8 *data = transaction.block(block, Ext2Transaction::Write);
        if (!data) return transaction.error();
        u32 within = position % block_size_;
        u32 count = block_size_ - within;
        if (count > size - copied) count = size - copied;
        memcpy(data + within, buffer + copied, count);
        copied += count;
    }
    if (offset + size > inode.i_size) inode.i_size = offset + size;
    transaction.store_inode(number, inode);
    error = transaction.commit();
    return error ? (u32)error : size;
}

int Ext2Mount::create_node(Ext2Directory *dir, const char *name, u32 mode, bool is_directory, File *&file) {
    IRQGuard guard;
    file = nullptr;
    if (!writable_) return -30;
    u32 length = 0;
    while (name && name[length] && length <= File::kMaxNameLength) ++length;
    if (!length || length > File::kMaxNameLength || !strcmp(name, ".") || !strcmp(name, "..")) return -22;
    for (u32 i = 0; i < length; ++i) if (name[i] == '/') return -22;

    ext2_inode directory;
    if (!read_inode(dir->inode(), &directory)) return -5;
    if ((directory.i_mode & EXT2_MODE_TYPE) != EXT2_MODE_DIR ||
        !(directory.i_mode & 0222) || !(directory.i_mode & 0111) ||
        (directory.i_flags & (EXT2_IMMUTABLE | EXT2_APPEND))) return -13;
    if (directory.i_flags & 0x1000) return -95;
    if (directory.i_links_count < 2) return -5;
    if (is_directory && directory.i_links_count == 0xffff) return -31;

    Ext2Transaction transaction(*this);
    if (!transaction.allocated_inode(dir->inode())) return transaction.error();
    u32 needed = (sizeof(ext2_dir_entry) + length + 3) & ~3U;
    u32 chosen_block = 0, chosen_offset = 0, chosen_split = 0;
    for (u32 i = 0; i < directory.i_size / block_size_; ++i) {
        u32 block;
        if (!get_block(directory, i, block) || !transaction.allocated_block(block)) return -5;
        u8 *data = transaction.block(block);
        if (!data) return transaction.error();
        for (u32 offset = 0; offset < block_size_;) {
            ext2_dir_entry *entry = (ext2_dir_entry *)(data + offset);
            if (entry->rec_len < sizeof(*entry) || (entry->rec_len & 3) ||
                entry->rec_len > block_size_ - offset ||
                entry->name_len > entry->rec_len - sizeof(*entry)) return -5;
            if (entry->inode && entry->name_len == length) {
                bool equal = true;
                for (u32 j = 0; j < length; ++j) if (entry->name[j] != name[j]) equal = false;
                if (equal) return -17;
            }
            u32 used = entry->inode ? (sizeof(*entry) + entry->name_len + 3) & ~3U : 0;
            if (!chosen_block && entry->rec_len - used >= needed) {
                chosen_block = block;
                chosen_offset = offset;
                chosen_split = used;
            }
            offset += entry->rec_len;
        }
    }

    u32 preferred = (dir->inode() - 1) / super_.s_inodes_per_group;
    u32 number = transaction.allocate_inode(preferred, is_directory);
    if (!number) return transaction.error();
    if (!chosen_block) {
        chosen_block = transaction.file_block(directory, directory.i_size / block_size_, preferred);
        if (!chosen_block) return transaction.error();
        directory.i_size += block_size_;
        u8 *data = transaction.block(chosen_block, Ext2Transaction::Write);
        if (!data) return transaction.error();
        ((ext2_dir_entry *)data)->rec_len = block_size_;
    }

    u8 *data = transaction.block(chosen_block, Ext2Transaction::Write);
    if (!data) return transaction.error();
    ext2_dir_entry *entry = (ext2_dir_entry *)(data + chosen_offset);
    u16 available = entry->rec_len - chosen_split;
    if (chosen_split) {
        entry->rec_len = chosen_split;
        entry = (ext2_dir_entry *)((u8 *)entry + chosen_split);
    }
    memset(entry, 0, available);
    entry->inode = number;
    entry->rec_len = available;
    entry->name_len = length;
    entry->file_type = (super_.s_feature_incompat & EXT2_FILETYPE) ? (is_directory ? 2 : 1) : 0;
    memcpy(entry->name, name, length);
    ext2_inode inode = {};
    inode.i_mode = (is_directory ? EXT2_MODE_DIR : EXT2_MODE_FILE) | (mode & 0777);
    inode.i_links_count = is_directory ? 2 : 1;
    if (is_directory) {
        u32 block = transaction.file_block(inode, 0, (number - 1) / super_.s_inodes_per_group);
        if (!block) return transaction.error();
        u8 *bytes = transaction.block(block, Ext2Transaction::Write);
        if (!bytes) return transaction.error();
        ext2_dir_entry *self = (ext2_dir_entry *)bytes;
        self->inode = number;
        self->rec_len = 12;
        self->name_len = 1;
        self->file_type = (super_.s_feature_incompat & EXT2_FILETYPE) ? 2 : 0;
        self->name[0] = '.';
        ext2_dir_entry *parent = (ext2_dir_entry *)(bytes + 12);
        parent->inode = dir->inode();
        parent->rec_len = block_size_ - 12;
        parent->name_len = 2;
        parent->file_type = self->file_type;
        parent->name[0] = parent->name[1] = '.';
        inode.i_size = block_size_;
        ++directory.i_links_count;
    }
    transaction.store_inode(number, inode);
    transaction.store_inode(dir->inode(), directory);

    File *created = is_directory ? static_cast<File *>(new Ext2Directory(name, this, number))
                                 : static_cast<File *>(new Ext2RegularFile(name, this, number, 0));
    if (!created || !created->getName()) { delete created; return -12; }
    created->setSize(inode.i_size);
    int error = transaction.commit();
    if (error) { delete created; return error; }
    if (dir->addChild(created) != RETURN_OK) { delete created; return -5; }
    dir->setSize(directory.i_size);
    file = created;
    return 0;
}

int Ext2Mount::truncate_file(u32 number, u32 size) {
    IRQGuard guard;
    ext2_inode inode;
    if (!read_inode(number, &inode)) return -5;
    int error = check_write(number, inode, false);
    if (error) return error;
    Ext2Transaction transaction(*this);
    if (!transaction.allocated_inode(number)) return transaction.error();
    transaction.truncate_inode(inode, size);
    transaction.store_inode(number, inode);
    return transaction.commit();
}

int Ext2Mount::reap_inode(u32 number, Ext2Node *closing) {
    IRQGuard guard;
    if (!super_.s_last_orphan || inode_referenced(number, closing)) return 0;
    if (!writable_) return -30;
    Ext2Transaction transaction(*this);
    ext2_inode inode;
    if (!transaction.remove_orphan(number, inode)) return transaction.error();
    transaction.release_inode(number, inode);
    return transaction.commit();
}

int Ext2Mount::empty_directory(Ext2Transaction &transaction, const ext2_inode &inode,
                               u32 number, u32 parent) {
    if (!inode.i_size || inode.i_size % block_size_ || inode.i_size / block_size_ > 12 + ptrs_per_block_)
        return -5;
    bool self_seen = false, parent_seen = false;
    for (u32 i = 0; i < inode.i_size / block_size_; ++i) {
        u32 block;
        if (!get_block(inode, i, block) || !transaction.allocated_block(block)) return -5;
        u8 *data = transaction.block(block);
        if (!data) return transaction.error();
        for (u32 offset = 0; offset < block_size_;) {
            ext2_dir_entry *entry = (ext2_dir_entry *)(data + offset);
            if (entry->rec_len < sizeof(*entry) || (entry->rec_len & 3) ||
                entry->rec_len > block_size_ - offset ||
                entry->name_len > entry->rec_len - sizeof(*entry) ||
                entry->inode > super_.s_inodes_count ||
                (!(super_.s_feature_incompat & EXT2_FILETYPE) && entry->file_type)) return -5;
            if (entry->inode) {
                if (entry->file_type && entry->file_type != 2) return -39;
                if (entry->name_len == 1 && entry->name[0] == '.') {
                    if (self_seen || entry->inode != number) return -5;
                    self_seen = true;
                } else if (entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.') {
                    if (parent_seen || entry->inode != parent) return -5;
                    parent_seen = true;
                } else {
                    return -39;
                }
            }
            offset += entry->rec_len;
        }
    }
    return self_seen && parent_seen ? 0 : -5;
}

int Ext2Mount::remove_node(Ext2Directory *dir, const char *name, bool is_directory) {
    IRQGuard guard;
    if (!writable_) return -30;
    File *file = dir->find(name);
    if (!file) return -2;
    if (is_directory && file->getType() != TYPE_DIRECTORY) return -20;
    if (!is_directory && file->getType() == TYPE_DIRECTORY) return -21;
    if (file->getType() != TYPE_FILE && file->getType() != TYPE_DIRECTORY) return -95;
    Ext2Node *node = static_cast<Ext2Node *>(file);
    ext2_inode directory, inode;
    if (!read_inode(dir->inode(), &directory) || !read_inode(node->inode(), &inode)) return -5;
    if (!(directory.i_mode & 0222) || !(directory.i_mode & 0111) ||
        (directory.i_flags & (EXT2_IMMUTABLE | EXT2_APPEND))) return -13;
    if (directory.i_flags & 0x1000) return -95;
    int error = 0;
    if (is_directory) {
        if (node->inode() == EXT2_ROOT_INO) return -16;
        if (node->inode() < (super_.s_rev_level ? super_.s_first_ino : 11)) return -13;
        if (inode.i_flags & (EXT2_IMMUTABLE | EXT2_APPEND)) return -13;
        if ((inode.i_mode & EXT2_MODE_TYPE) != EXT2_MODE_DIR) return -5;
    } else {
        error = check_write(node->inode(), inode, false);
        if (error) return error;
    }
    if (!inode.i_links_count) return -5;

    Ext2Transaction transaction(*this);
    if (!transaction.allocated_inode(dir->inode()) || !transaction.allocated_inode(node->inode()))
        return transaction.error();
    if (is_directory) {
        error = empty_directory(transaction, inode, node->inode(), dir->inode());
        if (error) return error;
        if (inode.i_links_count != 2 || directory.i_links_count < 3) return -5;
    }
    u32 length = 0;
    while (name[length]) ++length;
    bool found = false;
    for (u32 i = 0; !found && i < directory.i_size / block_size_; ++i) {
        u32 number;
        if (!get_block(directory, i, number) || !transaction.allocated_block(number)) return -5;
        u8 *data = transaction.block(number);
        if (!data) return transaction.error();
        ext2_dir_entry *previous = nullptr;
        for (u32 offset = 0; offset < block_size_;) {
            ext2_dir_entry *entry = (ext2_dir_entry *)(data + offset);
            if (entry->rec_len < sizeof(*entry) || (entry->rec_len & 3) ||
                entry->rec_len > block_size_ - offset ||
                entry->name_len > entry->rec_len - sizeof(*entry)) return -5;
            bool equal = entry->inode == node->inode() && entry->name_len == length;
            for (u32 j = 0; equal && j < length; ++j) equal = entry->name[j] == name[j];
            if (equal) {
                if (!transaction.block(number, Ext2Transaction::Write)) return transaction.error();
                if (previous) previous->rec_len += entry->rec_len;
                else {
                    entry->inode = 0;
                    entry->name_len = 0;
                    entry->file_type = 0;
                }
                found = true;
                break;
            }
            previous = entry;
            offset += entry->rec_len;
        }
    }
    if (!found) return -5;
    if (is_directory) {
        inode.i_links_count = 0;
        --directory.i_links_count;
        transaction.store_inode(dir->inode(), directory);
    } else {
        --inode.i_links_count;
    }
    if (!inode.i_links_count) {
        if (inode_referenced(node->inode())) {
            transaction.orphan_inode(node->inode(), inode);
            transaction.store_inode(node->inode(), inode);
        } else {
            transaction.release_inode(node->inode(), inode);
        }
    } else {
        transaction.store_inode(node->inode(), inode);
    }
    error = transaction.commit();
    if (error) return error;
    if (is_directory) static_cast<Ext2Directory *>(file)->keepParent(dir);
    file->File::remove();
    return 0;
}

Ext2Filesystem ext2_driver;

Ext2Node::Ext2Node(const char *name, u8 type, Ext2Mount *mount, u32 inode)
    : File(name, type, false), mount_(mount), inode_(inode), next_node_(nullptr) {
    if (mount_) {
        mount_->retain();
        mount_->attach_node(this);
    }
}

Ext2Node::~Ext2Node() {
    IRQGuard guard;
    if (mount_) {
        mount_->detach_node(this);
        mount_->reap_inode(inode_);
        mount_->release();
    }
}

u32 Ext2Node::close() {
    IRQGuard guard;
    return mount_ ? mount_->reap_inode(inode_, this) : 0;
}

Ext2Directory::Ext2Directory(const char *name, Ext2Mount *mount, u32 inode)
    : Ext2Node(name, TYPE_DIRECTORY, mount, inode), loaded_(false), former_parent_(nullptr) {}

Ext2Directory::~Ext2Directory() {
    IRQGuard guard;
    Ext2Directory *parent = former_parent_;
    former_parent_ = nullptr;
    while (parent) {
        Ext2Directory *next = nullptr;
        if (parent->isRemoved() && parent->references == 1) {
            next = parent->former_parent_;
            parent->former_parent_ = nullptr;
        }
        parent->release();
        parent = next;
    }
}

File *Ext2Directory::getParent() { return parent ? parent : former_parent_; }

void Ext2Directory::keepParent(Ext2Directory *file) {
    if (file) file->retain();
    Ext2Directory *previous = former_parent_;
    former_parent_ = file;
    if (previous) previous->release();
}

bool Ext2Directory::load() {
    if (!loaded_ && mount_) loaded_ = mount_->load_directory(this);
    return loaded_;
}

void Ext2Directory::scan() { load(); }

int Ext2Directory::createFile(const char *name, u32 mode, File *&file) {
    IRQGuard guard;
    file = nullptr;
    if (removed) return -2;
    if (!load()) return -5;
    return mount_->create_node(this, name, mode, false, file);
}

int Ext2Directory::unlinkFile(const char *name) {
    IRQGuard guard;
    if (removed) return -2;
    if (!load()) return -5;
    return mount_->remove_node(this, name, false);
}

int Ext2Directory::makeDirectory(const char *name, u32 mode) {
    IRQGuard guard;
    if (removed) return -2;
    if (!load()) return -5;
    File *file = nullptr;
    return mount_->create_node(this, name, mode, true, file);
}

int Ext2Directory::removeDirectory(const char *name) {
    IRQGuard guard;
    if (removed) return -2;
    if (!load()) return -5;
    return mount_->remove_node(this, name, true);
}

Ext2RegularFile::Ext2RegularFile(const char *name, Ext2Mount *mount, u32 inode, u32 size)
    : Ext2Node(name, TYPE_FILE, mount, inode) { setSize(size); }

int Ext2RegularFile::truncate(u32 size) {
    IRQGuard guard;
    int result = mount_ ? mount_->truncate_file(inode_, size) : -5;
    if (!result) setSize(size);
    return result;
}

u32 Ext2RegularFile::open(u32 flags) {
    if (!flags) return 0;
    ext2_inode inode;
    if (!mount_ || !mount_->read_inode(inode_, &inode)) return (u32)-5;
    int result = mount_->check_write(inode_, inode);
    if (!result && (flags & O_TRUNC)) result = truncate(0);
    return result;
}

u32 Ext2RegularFile::getSize() {
    ext2_inode inode;
    if (mount_ && mount_->read_inode(inode_, &inode)) setSize(inode.i_size);
    return File::getSize();
}

u32 Ext2RegularFile::read(u32 pos, u8 *buffer, u32 size) {
    ext2_inode inode;
    if (!mount_ || !mount_->read_inode(inode_, &inode)) return (u32)-5;
    return mount_->read_file_data(inode, pos, buffer, size);
}

u32 Ext2RegularFile::write(u32 pos, u8 *buffer, u32 size) {
    IRQGuard guard;
    ext2_inode inode;
    if (!mount_ || !mount_->read_inode(inode_, &inode)) return (u32)-5;
    u32 result = mount_->write_file_data(inode_, inode, pos, buffer, size);
    if ((s32)result > 0) getSize();
    return result;
}

Ext2Filesystem::Ext2Filesystem() = default;
const char *Ext2Filesystem::name() const { return "ext2"; }

File *Ext2Filesystem::mount(File *mount_point, BlockDevice *device, bool writable) {
    if (!mount_point || !device) return nullptr;
    Ext2Mount *mount = new Ext2Mount(device, writable);
    if (!mount || !mount->initialize()) { delete mount; return nullptr; }
    Ext2Directory *root = new Ext2Directory(mount_point->getName(), mount, EXT2_ROOT_INO);
    if (!root) { delete mount; return nullptr; }
    if (!root->getName() || !root->load()) { delete root; return nullptr; }
    File *parent = mount_point->getParent();
    mount_point->remove();
    if (parent) parent->addChild(root);
    return root;
}

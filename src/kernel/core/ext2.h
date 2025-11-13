#ifndef EXT2_H
#define EXT2_H

#include <filesystem_driver.h>
#include <core/file.h>

class Ext2Mount;

struct ext2_super_block {
    u32 s_inodes_count;
    u32 s_blocks_count;
    u32 s_r_blocks_count;
    u32 s_free_blocks_count;
    u32 s_free_inodes_count;
    u32 s_first_data_block;
    u32 s_log_block_size;
    u32 s_log_frag_size;
    u32 s_blocks_per_group;
    u32 s_frags_per_group;
    u32 s_inodes_per_group;
    u32 s_mtime;
    u32 s_wtime;
    u16 s_mnt_count;
    u16 s_max_mnt_count;
    u16 s_magic;
    u16 s_state;
    u16 s_errors;
    u16 s_minor_rev_level;
    u32 s_lastcheck;
    u32 s_checkinterval;
    u32 s_creator_os;
    u32 s_rev_level;
    u16 s_def_resuid;
    u16 s_def_resgid;
    u32 s_first_ino;
    u16 s_inode_size;
    u16 s_block_group_nr;
    u32 s_feature_compat;
    u32 s_feature_incompat;
    u32 s_feature_ro_compat;
    u8  s_uuid[16];
    char s_volume_name[16];
    char s_last_mounted[64];
    u32 s_algorithm_usage_bitmap;
    u8  s_prealloc_blocks;
    u8  s_prealloc_dir_blocks;
    u16 s_padding1;
    u8  s_journal_uuid[16];
    u32 s_journal_inum;
    u32 s_journal_dev;
    u32 s_last_orphan;
    u32 s_hash_seed[4];
    u8  s_def_hash_version;
    u8  s_reserved_char_pad;
    u16 s_reserved_word_pad;
    u32 s_default_mount_opts;
    u32 s_first_meta_bg;
    u32 s_reserved[190];
} __attribute__((packed));

struct ext2_group_desc {
    u32 bg_block_bitmap;
    u32 bg_inode_bitmap;
    u32 bg_inode_table;
    u16 bg_free_blocks_count;
    u16 bg_free_inodes_count;
    u16 bg_used_dirs_count;
    u16 bg_pad;
    u32 bg_reserved[3];
} __attribute__((packed));

struct ext2_inode {
    u16 i_mode;
    u16 i_uid;
    u32 i_size;
    u32 i_atime;
    u32 i_ctime;
    u32 i_mtime;
    u32 i_dtime;
    u16 i_gid;
    u16 i_links_count;
    u32 i_blocks;
    u32 i_flags;
    u32 i_osd1;
    u32 i_block[15];
    u32 i_generation;
    u32 i_file_acl;
    u32 i_dir_acl;
    u32 i_faddr;
    u32 i_osd2[3];
} __attribute__((packed));

struct ext2_dir_entry {
    u32 inode;
    u16 rec_len;
    u8 name_len;
    u8 file_type;
    char name[];
} __attribute__((packed));

class Ext2Node : public File {
public:
    Ext2Node(const char* name, u8 type, Ext2Mount* mount, u32 inode);
    virtual ~Ext2Node();

    Ext2Mount* mount() const { return mount_; }
    u32 inode() const { return inode_; }

protected:
    Ext2Mount* mount_;
    u32 inode_;
};

class Ext2Directory : public Ext2Node {
public:
    Ext2Directory(const char* name, Ext2Mount* mount, u32 inode, bool owns_mount);
    virtual ~Ext2Directory();
    virtual void scan() override;

private:
    bool loaded_;
    bool owns_mount_;
};

class Ext2RegularFile : public Ext2Node {
public:
    Ext2RegularFile(const char* name, Ext2Mount* mount, u32 inode, u32 size);
    virtual u32 read(u32 pos, u8* buffer, u32 size) override;
};

class Ext2Filesystem : public FilesystemDriver {
public:
    Ext2Filesystem();
    virtual const char* name() const override;
    virtual File* mount(File* mount_point, BlockDevice* device) override;
};

extern Ext2Filesystem ext2_driver;

#endif // EXT2_H

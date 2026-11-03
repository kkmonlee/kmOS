#include <os.h>
#include <filesystem.h>
#include <ext2.h>
#include <block_device.h>
#include <ata.h>
#include <serial.h>

#define CHECK(condition) do { if (!(condition)) { \
    serial::write("[STORAGE] failed line="); serial::hex(__LINE__); \
    serial::write("\n"); return false; \
} } while (0)

extern "C" {
    void *memset(void *, int, int);
    void *memcpy(void *, const void *, int);
}

static const char message[] = "disk read verified\n";

class TestDisk : public BlockDevice {
public:
    u8 bytes[65536];
    u32 block_size, root_block, file_block, inode_table, fail_block;
    TestDisk() : BlockDevice("storage-test") { reset(1024); }
    void reset(u32 size) {
        memset(bytes, 0, sizeof(bytes));
        block_size = size;
        fail_block = ~0U;
        u32 descriptor = size == 1024 ? 2 : 1;
        inode_table = descriptor + 3;
        root_block = inode_table + (16 * 128 + size - 1) / size;
        file_block = root_block + 1;
        ext2_super_block *sb = super();
        sb->s_magic = 0xef53;
        sb->s_rev_level = 1;
        sb->s_first_ino = 11;
        sb->s_blocks_count = 16;
        sb->s_inodes_count = sb->s_inodes_per_group = 16;
        sb->s_blocks_per_group = 16;
        sb->s_first_data_block = size == 1024;
        sb->s_log_block_size = size == 1024 ? 0 : (size == 2048 ? 1 : 2);
        sb->s_inode_size = 128;
        sb->s_feature_incompat = 2;
        ext2_group_desc *group = (ext2_group_desc *)(bytes + descriptor * size);
        group->bg_block_bitmap = descriptor + 1;
        group->bg_inode_bitmap = descriptor + 2;
        group->bg_inode_table = inode_table;
        inode(2)->i_mode = 0x41ed;
        inode(2)->i_size = size;
        inode(2)->i_block[0] = root_block;
        inode(11)->i_mode = 0x8124;
        inode(11)->i_size = sizeof(message) - 1;
        inode(11)->i_block[0] = file_block;
        entry(0, 2, 12, 2, ".", 1);
        entry(12, 2, 12, 2, "..", 2);
        entry(24, 11, size - 24, 1, "note", 4);
        memcpy(bytes + file_block * size, message, sizeof(message) - 1);
    }
    ext2_super_block *super() { return (ext2_super_block *)(bytes + 1024); }
    ext2_inode *inode(u32 number) {
        return (ext2_inode *)(bytes + inode_table * block_size + (number - 1) * 128);
    }
    ext2_dir_entry *record(u32 offset) { return (ext2_dir_entry *)(bytes + root_block * block_size + offset); }
    void entry(u32 offset, u32 ino, u16 length, u8 type, const char *name, u8 size) {
        ext2_dir_entry *record = this->record(offset);
        record->inode = ino;
        record->rec_len = length;
        record->name_len = size;
        record->file_type = type;
        memcpy(record->name, name, size);
    }
    u32 read_blocks(u32 lba, u32 count, void *buffer) override {
        if (!buffer || !count || lba >= sizeof(bytes) / 512 || count > sizeof(bytes) / 512 - lba)
            return ERROR_PARAM;
        if (fail_block != ~0U && lba * 512 < (fail_block + 1) * block_size &&
            (lba + count) * 512 > fail_block * block_size) return RETURN_FAILURE;
        memcpy(buffer, bytes + lba * 512, count * 512);
        return RETURN_OK;
    }
    u32 write_blocks(u32, u32, const void *) override { return RETURN_FAILURE; }
};

static bool valid_images(TestDisk *disk) {
    const u32 sizes[] = {1024, 2048, 4096};
    for (u32 size : sizes) {
        disk->reset(size);
        File *point = fsm.path("/mnt")->createChild("storage-test", TYPE_DIRECTORY);
        CHECK(point && fsm.mount(disk, "/mnt/storage-test", "ext2"));
        File *root = fsm.path("/mnt/storage-test");
        File *file = fsm.path("/mnt/storage-test/note");
        CHECK(root && root != point && file && file->getSize() == sizeof(message) - 1);
        u8 buffer[64];
        memset(buffer, 0xcc, sizeof(buffer));
        CHECK(file->read(1, buffer, ~0U) == sizeof(message) - 2);
        for (u32 i = 0; i < sizeof(message) - 2; ++i) CHECK(buffer[i] == message[i + 1]);
        CHECK(buffer[sizeof(message) - 2] == 0xcc);
        CHECK(file->read(file->getSize(), buffer, 1) == 0 && file->open(1) == (u32)-30);
        disk->fail_block = disk->file_block;
        CHECK(file->read(0, buffer, 1) == (u32)-5);
        disk->fail_block = ~0U;
        disk->inode(11)->i_size = 2 * size;
        disk->inode(11)->i_block[0] = 0;
        disk->inode(11)->i_block[1] = disk->file_block;
        CHECK(file->read(size - 5, buffer, sizeof(message) + 4) == sizeof(message) + 4);
        for (u32 i = 0; i < 5; ++i) CHECK(!buffer[i]);
        for (u32 i = 0; i < sizeof(message) - 1; ++i) CHECK(buffer[i + 5] == message[i]);
        disk->inode(11)->i_size = 13 * size;
        disk->inode(11)->i_block[12] = 10;
        *(u32 *)(disk->bytes + 10 * size) = 11;
        disk->bytes[11 * size] = 0x79;
        CHECK(file->read(12 * size, buffer, 1) == 1 && buffer[0] == 0x79);
        disk->inode(11)->i_block[12] = ~0U;
        CHECK(file->read(12 * size, buffer, 1) == (u32)-5);
        disk->inode(11)->i_size = sizeof(message) - 1;
        disk->inode(11)->i_block[0] = disk->file_block;
        file->retain();
        root->remove();
        CHECK(file->read(0, buffer, sizeof(buffer)) == sizeof(message) - 1);
        file->release();
    }
    return true;
}

bool verify_storage() {
    TestDisk *disk = new TestDisk();
    CHECK(disk);
    for (u32 which = 0; which < 15; ++which) {
        disk->reset(1024);
        switch (which) {
        case 0: disk->super()->s_blocks_per_group = 0; break;
        case 1: disk->super()->s_inodes_per_group = 0; break;
        case 2: disk->super()->s_log_block_size = 32; break;
        case 3: disk->super()->s_inode_size = 64; break;
        case 4: disk->super()->s_inode_size = 192; break;
        case 5: disk->super()->s_blocks_count = ~0U; break;
        case 6: disk->super()->s_feature_incompat = 0x40; break;
        case 7: ((ext2_group_desc *)(disk->bytes + 2048))->bg_inode_table = 15; break;
        case 8: disk->record(24)->rec_len = 0; break;
        case 9: disk->record(24)->rec_len = 6; break;
        case 10: disk->record(24)->name_len = 255; disk->record(24)->rec_len = 12; break;
        case 11: disk->record(24)->inode = 17; break;
        case 12: disk->inode(2)->i_size = 1025; break;
        case 13: disk->record(24)->rec_len = 16; break;
        case 14: disk->fail_block = 2; break;
        }
        File *point = fsm.path("/mnt")->createChild("storage-test", TYPE_DIRECTORY);
        CHECK(point && !fsm.mount(disk, "/mnt/storage-test", "ext2"));
        CHECK(fsm.path("/mnt/storage-test") == point && !point->getChild());
        point->remove();
    }
    CHECK(valid_images(disk));
    delete disk;
    serial::write("[STORAGE] ext2 geometry, records, reads and errors verified\n");
    return true;
}

bool verify_ata() {
    ata_init();
    ATADevice *disk = ata_primary_master();
    CHECK(disk && disk->identify().total_sectors == 16);
    u8 original[512], written[1024], read[1024];
    CHECK(disk->read_blocks(0, 1, original) == RETURN_OK);
    for (u32 i = 0; i < sizeof(original); ++i) CHECK(original[i] == 0x5a);
    for (u32 i = 0; i < sizeof(written); ++i) written[i] = (u8)(i * 17 + 3);
    const u32 invalid[][2] = {{0x10000000, 1}, {~0U, 1}, {16, 1}, {15, 2}, {1, ~0U}, {0, 0}};
    for (const auto &range : invalid) {
        CHECK(disk->read_blocks(range[0], range[1], read) == (u32)ERROR_PARAM);
        CHECK(disk->write_blocks(range[0], range[1], written) == (u32)ERROR_PARAM);
    }
    CHECK(disk->read_blocks(0, 1, nullptr) == (u32)ERROR_PARAM);
    CHECK(disk->write_blocks(0, 1, nullptr) == (u32)ERROR_PARAM);
    CHECK(disk->read_blocks(0, 1, read) == RETURN_OK);
    for (u32 i = 0; i < 512; ++i) CHECK(read[i] == original[i]);
    CHECK(disk->write_blocks(1, 2, written) == RETURN_OK);
    CHECK(disk->read_blocks(1, 2, read) == RETURN_OK);
    for (u32 i = 0; i < sizeof(read); ++i) CHECK(read[i] == written[i]);
    CHECK(disk->read_blocks(15, 1, read) == RETURN_OK);
    for (u32 i = 0; i < 512; ++i) CHECK(read[i] == 0x7b);
    serial::write("[STORAGE] ATA bounds, transfers and flush verified\n");
    return true;
}

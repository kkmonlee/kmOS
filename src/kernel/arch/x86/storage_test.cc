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
    u32 writes, fail_write_after;
    TestDisk() : BlockDevice("storage-test") { reset(1024); }
    void reset(u32 size) {
        memset(bytes, 0, sizeof(bytes));
        block_size = size;
        fail_block = ~0U;
        writes = 0;
        fail_write_after = ~0U;
        u32 descriptor = size == 1024 ? 2 : 1;
        inode_table = descriptor + 3;
        root_block = inode_table + (16 * 128 + size - 1) / size;
        file_block = root_block + 1;
        ext2_super_block *sb = super();
        sb->s_magic = 0xef53;
        sb->s_state = 1;
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
        sb->s_free_blocks_count = group->bg_free_blocks_count = 16 - file_block - 1;
        sb->s_free_inodes_count = group->bg_free_inodes_count = 5;
        group->bg_used_dirs_count = 1;
        u8 *blocks = bytes + group->bg_block_bitmap * size;
        memset(blocks, 0xff, size);
        for (u32 block = file_block + 1; block < 16; ++block) {
            u32 bit = block - sb->s_first_data_block;
            blocks[bit / 8] &= ~(1U << (bit % 8));
        }
        u8 *inodes = bytes + group->bg_inode_bitmap * size;
        memset(inodes, 0xff, size);
        for (u32 bit = 11; bit < 16; ++bit) inodes[bit / 8] &= ~(1U << (bit % 8));
        inode(2)->i_links_count = 2;
        inode(2)->i_blocks = size / 512;
        inode(11)->i_links_count = 1;
        inode(11)->i_blocks = size / 512;
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
    u32 write_blocks(u32 lba, u32 count, const void *buffer) override {
        if (!buffer || !count || lba >= sizeof(bytes) / 512 || count > sizeof(bytes) / 512 - lba)
            return ERROR_PARAM;
        if (writes++ >= fail_write_after) return RETURN_FAILURE;
        memcpy(bytes + lba * 512, buffer, count * 512);
        return RETURN_OK;
    }
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

static File *write_mount(TestDisk *disk) {
    File *point = new File("storage-write", TYPE_DIRECTORY);
    if (!point) return nullptr;
    File *root = ext2_driver.mount(point, disk, true);
    if (!root) delete point;
    return root;
}

bool verify_storage_writes() {
    TestDisk *disk = new TestDisk();
    CHECK(disk);
    File *root = write_mount(disk);
    CHECK(root);
    File *file = nullptr;
    CHECK(root->createFile("created", 0644, file) == 0 && file);
    u32 creation_writes = disk->writes;
    root->remove();

    for (u32 failure = 0; failure < creation_writes; ++failure) {
        disk->reset(1024);
        root = write_mount(disk);
        CHECK(root);
        disk->fail_write_after = failure;
        file = nullptr;
        CHECK(root->createFile("created", 0644, file) == -5 && !file);
        CHECK(!root->find("created"));
        u32 writes = disk->writes;
        CHECK(root->createFile("retry", 0644, file) == -30 && disk->writes == writes);
        CHECK(disk->super()->s_state == (failure ? 0 : 1));
        root->remove();
        if (failure) CHECK(!write_mount(disk));
    }

    disk->reset(1024);
    root = write_mount(disk);
    CHECK(root && root->createFile("created", 0644, file) == 0 && file);
    u8 *snapshot = (u8 *)kmalloc(sizeof(disk->bytes));
    u8 *data = (u8 *)kmalloc(8 * 1024);
    CHECK(snapshot && data);
    memset(data, 0x3a, 8 * 1024);
    memcpy(snapshot, disk->bytes, sizeof(disk->bytes));
    u32 writes = disk->writes;
    CHECK(file->write(0, data, 8 * 1024) == (u32)-28);
    CHECK(disk->writes == writes && file->getSize() == 0);
    for (u32 i = 0; i < sizeof(disk->bytes); ++i) CHECK(disk->bytes[i] == snapshot[i]);
    CHECK(file->write(0, data, 2048) == 2048 && file->getSize() == 2048);
    u32 growth_writes = disk->writes - writes;
    kfree(snapshot);
    root->remove();

    for (u32 failure = 0; failure < growth_writes; ++failure) {
        disk->reset(1024);
        root = write_mount(disk);
        CHECK(root && root->createFile("created", 0644, file) == 0 && file);
        disk->fail_write_after = disk->writes + failure;
        CHECK(file->write(0, data, 2048) == (u32)-5);
        writes = disk->writes;
        CHECK(file->write(0, data, 1) == (u32)-30 && disk->writes == writes);
        CHECK(disk->super()->s_state == (failure ? 0 : 1));
        root->remove();
    }
    for (u32 which = 0; which < 2; ++which) {
        disk->reset(1024);
        disk->inode(11)->i_mode = 0x81a4;
        disk->inode(11)->i_size = 2048;
        disk->inode(11)->i_block[1] = disk->file_block;
        if (which) disk->inode(11)->i_blocks = ~0U;
        root = write_mount(disk);
        CHECK(root);
        file = root->find("note");
        CHECK(file && file->write(0, data, 2048) == (u32)-5);
        CHECK(!disk->writes && disk->super()->s_state == 1);
        root->remove();
    }
    kfree(data);
    delete disk;
    serial::write("[STORAGE] Allocation rollback and write failures verified\n");
    return true;
}

static File *removal_mount(TestDisk *disk, File *&file) {
    disk->reset(1024);
    File *root = write_mount(disk);
    if (!root) return nullptr;
    u8 bytes[2048];
    memset(bytes, 0x3a, sizeof(bytes));
    if (root->createFile("removed", 0644, file) || file->write(0, bytes, sizeof(bytes)) != sizeof(bytes)) {
        root->remove();
        return nullptr;
    }
    return root;
}

bool verify_storage_removal() {
    TestDisk *disk = new TestDisk();
    CHECK(disk);
    for (u32 operation = 0; operation < 3; ++operation) {
        File *file = nullptr;
        File *root = removal_mount(disk, file);
        CHECK(root && file);
        if (operation == 2) {
            file->retain();
            CHECK(root->unlinkFile("removed") == 0);
        }
        u32 before = disk->writes;
        int result = operation == 0 ? file->truncate(1) :
            (operation == 1 ? root->unlinkFile("removed") : (int)file->close());
        CHECK(result == 0);
        u32 count = disk->writes - before;
        if (operation == 2) file->release();
        root->remove();
        for (u32 failure = 0; failure < count; ++failure) {
            root = removal_mount(disk, file);
            CHECK(root && file);
            if (operation == 2) {
                file->retain();
                CHECK(root->unlinkFile("removed") == 0);
            }
            disk->fail_write_after = disk->writes + failure;
            result = operation == 0 ? file->truncate(1) :
                (operation == 1 ? root->unlinkFile("removed") : (int)file->close());
            CHECK(result == -5);
            u32 writes = disk->writes;
            result = operation == 0 ? file->truncate(1) :
                (operation == 1 ? root->unlinkFile("removed") : (int)file->close());
            CHECK(result == -30 && writes == disk->writes);
            CHECK(disk->super()->s_state == ((failure || operation == 2) ? 0 : 1));
            if (operation == 2) file->release();
            root->remove();
        }
    }
    delete disk;
    serial::write("[STORAGE] Truncate, unlink and close failures verified\n");
    return true;
}

bool verify_storage_directories() {
    TestDisk *disk = new TestDisk();
    CHECK(disk);
    for (u32 operation = 0; operation < 3; ++operation) {
        disk->reset(1024);
        File *root = write_mount(disk);
        CHECK(root);
        File *directory = nullptr;
        if (operation) {
            CHECK(root->makeDirectory("directory", 0755) == 0);
            directory = root->find("directory");
            CHECK(directory);
        }
        if (operation == 2) {
            directory->retain();
            CHECK(root->removeDirectory("directory") == 0);
        }
        u32 before = disk->writes;
        int result = operation == 0 ? root->makeDirectory("directory", 0755) :
            (operation == 1 ? root->removeDirectory("directory") : (int)directory->close());
        CHECK(result == 0);
        u32 count = disk->writes - before;
        if (operation == 2) directory->release();
        root->remove();

        for (u32 failure = 0; failure < count; ++failure) {
            disk->reset(1024);
            root = write_mount(disk);
            CHECK(root);
            if (operation) {
                CHECK(root->makeDirectory("directory", 0755) == 0);
                directory = root->find("directory");
                CHECK(directory);
            }
            if (operation == 2) {
                directory->retain();
                CHECK(root->removeDirectory("directory") == 0);
            }
            disk->fail_write_after = disk->writes + failure;
            result = operation == 0 ? root->makeDirectory("directory", 0755) :
                (operation == 1 ? root->removeDirectory("directory") : (int)directory->close());
            CHECK(result == -5);
            u32 writes = disk->writes;
            result = operation == 0 ? root->makeDirectory("directory", 0755) :
                (operation == 1 ? root->removeDirectory("directory") : (int)directory->close());
            CHECK(result == -30 && disk->writes == writes);
            CHECK(disk->super()->s_state == ((failure || operation == 2) ? 0 : 1));
            if (operation == 2) directory->release();
            root->remove();
        }
    }

    disk->reset(1024);
    File *root = write_mount(disk);
    File *file = nullptr;
    CHECK(root && root->createFile("full", 0644, file) == 0);
    u8 *data = (u8 *)kmalloc(7 * 1024);
    CHECK(data);
    memset(data, 0x5a, 7 * 1024);
    CHECK(file->write(0, data, 7 * 1024) == 7 * 1024);
    u32 writes = disk->writes;
    u32 free_inodes = disk->super()->s_free_inodes_count;
    CHECK(root->makeDirectory("full-directory", 0755) == -28 && disk->writes == writes);
    CHECK(disk->super()->s_free_inodes_count == free_inodes && disk->super()->s_state == 1);
    CHECK(root->unlinkFile("full") == 0 && root->makeDirectory("reused", 0755) == 0);
    CHECK(root->removeDirectory("reused") == 0);
    kfree(data);
    root->remove();

    disk->reset(1024);
    root = write_mount(disk);
    CHECK(root);
    for (u32 i = 0; i < 5; ++i) {
        char name[] = "dir0";
        name[3] += i;
        CHECK(root->makeDirectory(name, 0755) == 0);
    }
    writes = disk->writes;
    CHECK(root->makeDirectory("no-inode", 0755) == -28 && disk->writes == writes);
    for (u32 i = 0; i < 5; ++i) {
        char name[] = "dir0";
        name[3] += i;
        CHECK(root->removeDirectory(name) == 0);
    }
    CHECK(disk->super()->s_free_inodes_count == 5);
    root->remove();

    for (u32 which = 0; which < 3; ++which) {
        disk->reset(1024);
        root = write_mount(disk);
        CHECK(root && root->makeDirectory("malformed", 0755) == 0);
        Ext2Node *node = static_cast<Ext2Node *>(root->find("malformed"));
        CHECK(node);
        u32 block = disk->inode(node->inode())->i_block[0];
        ext2_dir_entry *self = (ext2_dir_entry *)(disk->bytes + block * 1024);
        ext2_dir_entry *parent = (ext2_dir_entry *)((u8 *)self + 12);
        if (!which) self->inode = 2;
        else if (which == 1) parent->inode = node->inode();
        else self->inode = 0;
        writes = disk->writes;
        CHECK(root->removeDirectory("malformed") == -5 && disk->writes == writes);
        root->remove();
    }
    disk->reset(1024);
    disk->record(24)->inode = 7;
    disk->record(24)->file_type = 2;
    disk->inode(7)->i_mode = 0x41ed;
    disk->inode(7)->i_size = 1024;
    disk->inode(7)->i_block[0] = 9;
    root = write_mount(disk);
    CHECK(root && root->removeDirectory("note") == -13 && !disk->writes);
    root->remove();
    delete disk;
    serial::write("[STORAGE] Directory allocation, validation and write failures verified\n");
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

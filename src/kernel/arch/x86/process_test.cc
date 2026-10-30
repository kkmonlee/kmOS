#include <os.h>
#include <process.h>
#include <filesystem.h>
#include <serial.h>

#define CHECK(test) do { if (!(test)) { \
    serial::write("[PROC] failed line="); serial::hex(__LINE__); serial::write("\n"); return false; \
} } while (0)

struct Counts { u32 opens, closes, destroyed; };

class TrackedFile : public File {
public:
    TrackedFile(Counts *counts) : File("tracked", TYPE_FILE), counts(counts) { setSize(6); }
    ~TrackedFile() { ++counts->destroyed; }
    u32 open(u32) override { ++counts->opens; return 0; }
    u32 close() override { ++counts->closes; return 0; }
    u32 read(u32 offset, u8 *buffer, u32 count) override {
        if (offset >= 6) return 0;
        if (count > 6 - offset) count = 6 - offset;
        for (u32 i = 0; i < count; ++i) buffer[i] = "abcdef"[offset + i];
        return count;
    }
private:
    Counts *counts;
};

bool verify_process_files() {
    Counts first = {}, second = {};
    Process *process = new Process(const_cast<char *>("descriptor_test"));
    TrackedFile *one = new TrackedFile(&first), *two = new TrackedFile(&second);
    CHECK(process->addFile(one, 0) == 0 && process->addFile(two, 0) == 1);
    CHECK(process->duplicateFile(0, 1) == 1 && second.closes == 1);
    CHECK(process->getFileInfo(0) == process->getFileInfo(1));
    process->getFileInfo(0)->ptr = 4;
    CHECK(process->getFileInfo(1)->ptr == 4);
    CHECK(process->duplicateFile(1, 1) == 1 && first.closes == 0);
    CHECK(process->duplicateFile(31, 1) == -9 && process->getFile(1) == one);
    CHECK(process->deleteFile(0) == 0 && first.closes == 0);
    CHECK(one->remove() == 0 && !first.destroyed && process->getFile(1) == one);
    u8 buffer[6];
    CHECK(one->read(4, buffer, sizeof(buffer)) == 2 && buffer[0] == 'e' && buffer[1] == 'f');
    CHECK(process->deleteFile(1) == 0 && first.closes == 1 && first.destroyed == 1);
    CHECK(process->deleteFile(1) == -9);
    CHECK(process->addFile(two, 0) == 0 && process->addFile(two, 0) == 1);
    CHECK(process->getFileInfo(0) != process->getFileInfo(1));
    delete process;
    CHECK(second.opens == 3 && second.closes == 3 && !second.destroyed);
    delete two;
    CHECK(second.destroyed == 1);
    Counts forced = {};
    process = new Process(const_cast<char *>("deleted_file_test"));
    one = new TrackedFile(&forced);
    CHECK(process->addFile(one, 0) == 0);
    delete one;
    CHECK(!process->getFile(0));
    delete process;
    CHECK(forced.destroyed == 1);
    File *directory = fsm.path("/mnt")->createChild("held", TYPE_DIRECTORY);
    process = new Process(const_cast<char *>("cwd_test"));
    CHECK(directory && process->setCurrentDir(directory));
    CHECK(directory->remove() == 0 && !fsm.path("/mnt/held"));
    CHECK(process->getCurrentDir() == directory && directory->getType() == TYPE_DIRECTORY);
    delete process;
    serial::write("[PROC] descriptor references, close, removal and cwd verified\n");
    return true;
}

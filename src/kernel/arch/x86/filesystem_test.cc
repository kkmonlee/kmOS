#include <os.h>
#include <filesystem.h>
#include <shell.h>
#include <architecture.h>
#include <runtime/unified_alloc.h>
#include <serial.h>

extern "C" int strcmp(const char *, const char *);

#define CHECK(condition) do { if (!(condition)) { \
    serial::write("[FS] failed line="); \
    serial::hex(__LINE__); \
    serial::write("\n"); \
    return false; \
} } while (0)

static u32 destroyed;

class CountedFile : public File {
public:
    CountedFile(const char *name) : File(name, TYPE_FILE) {}
    ~CountedFile() override { ++destroyed; }
};

class TextFile : public File {
public:
    TextFile() : File("message", TYPE_FILE) { setSize(19); }
    u32 read(u32 pos, u8 *buffer, u32 count) override {
        const char text[] = "filesystem read ok\n";
        if (pos >= sizeof(text) - 1) return 0;
        if (count > sizeof(text) - 1 - pos) count = sizeof(text) - 1 - pos;
        for (u32 i = 0; i < count; ++i) buffer[i] = text[pos + i];
        return count;
    }
};

static int command(const char *name, const char *argument = nullptr) {
    char *args[] = {const_cast<char *>(name), const_cast<char *>(argument), nullptr};
    return shell.execute_command(argument ? 2 : 1, args);
}

static bool paths(File *root, File *directory) {
    CHECK(!fsm.path(nullptr) && !fsm.path(""));
    CHECK(fsm.path(".") == root && fsm.path("dev") == fsm.path("/dev"));
    CHECK(fsm.path("/../../..") == root && fsm.path("..") == root);
    CHECK(fsm.path("///sys//env/.././") == fsm.path("/sys"));
    CHECK(!fsm.path("/missing/child") && !fsm.path("missing"));

    char leaf[File::kMaxNameLength + 1];
    File *env = fsm.path("/sys/env");
    CHECK(fsm.path_parent("/sys/env/new", leaf, sizeof(leaf)) == env);
    CHECK(!strcmp(leaf, "new"));
    CHECK(fsm.path_parent("/sys/env", leaf, sizeof(leaf)) == fsm.path("/sys"));
    CHECK(!strcmp(leaf, "env"));
    CHECK(!fsm.path_parent("/missing/child", leaf, sizeof(leaf)));
    CHECK(!fsm.path_parent("/sys/new/", leaf, sizeof(leaf)));
    CHECK(!fsm.path_parent("/", leaf, sizeof(leaf)));
    CHECK(!fsm.path_parent(".", leaf, sizeof(leaf)));
    CHECK(!fsm.path_parent("..", leaf, sizeof(leaf)));
    CHECK(!fsm.path_parent("new", nullptr, 0));
    struct { char value[4]; u32 guard; } small = {{0}, 0x12345678};
    CHECK(!fsm.path_parent("/sys/longer", small.value, sizeof(small.value)));
    CHECK(small.guard == 0x12345678);

    char path[512];
    CHECK(fsm.get_path(root, path, 2) && !strcmp(path, "/"));
    CHECK(!fsm.get_path(root, path, 1));
    CHECK(fsm.get_path(env, path, sizeof(path)) && !strcmp(path, "/sys/env"));
    CHECK(!fsm.get_path(env, path, 4));

    char name[File::kMaxNameLength + 2];
    for (u32 i = 0; i < File::kMaxNameLength; ++i) name[i] = 'n';
    name[File::kMaxNameLength] = 0;
    File *long_name = directory->createChild(name, TYPE_FILE);
    CHECK(long_name && arch.pcurrent->setCurrentDir(directory));
    CHECK(fsm.path(name) == long_name);
    name[File::kMaxNameLength] = 'n';
    name[File::kMaxNameLength + 1] = 0;
    CHECK(!fsm.path(name) && !directory->createChild(name, TYPE_FILE));
    CHECK(!fsm.path_parent(name, leaf, sizeof(leaf)));
    static char too_long[Filesystem::kMaxPathLength + 1];
    for (u32 i = 0; i < Filesystem::kMaxPathLength; ++i) too_long[i] = '/';
    CHECK(!fsm.path(too_long));
    delete long_name;
    CHECK(arch.pcurrent->setCurrentDir(root));
    return true;
}

static bool links(File *directory) {
    CHECK(fsm.link("/sys", "/mnt/test/system") == RETURN_OK);
    CHECK(fsm.path("/mnt/test/system/env/..") == fsm.path("/sys"));
    CHECK(fsm.link("/sys", "/mnt/test/system") != RETURN_OK);
    CHECK(fsm.link("/missing", "/mnt/test/absent") != RETURN_OK);
    CHECK(fsm.link("/sys", "/missing/absent") != RETURN_OK);
    CHECK(fsm.link("/sys", "/mnt/test/trailing/") != RETURN_OK);
    CHECK(fsm.link("/sys", "/mnt/test/.") != RETURN_OK);

    File *target = directory->createChild("target", TYPE_FILE);
    CHECK(target && fsm.link("/mnt/test/target", "/mnt/test/alias") == RETURN_OK);
    File *alias = directory->find("alias");
    CHECK(alias && fsm.path("/mnt/test/alias") == target);
    delete target;
    CHECK(!alias->getLink() && !fsm.path("/mnt/test/alias"));

    File *first = directory->createChild("first", TYPE_LINK);
    File *second = directory->createChild("second", TYPE_LINK);
    CHECK(first && second);
    first->setLink(second);
    second->setLink(first);
    CHECK(!fsm.path("/mnt/test/first"));
    delete first;
    CHECK(!second->getLink());
    second->setLink(second);
    CHECK(!fsm.path("/mnt/test/second"));
    delete second;
    return true;
}

static bool navigation(File *root, File *directory) {
    shell.init();
    CHECK(directory->addChild(new TextFile()) == RETURN_OK);
    File *unreadable = directory->createChild("unreadable", TYPE_FILE);
    CHECK(unreadable);
    unreadable->setSize(3);
    CHECK(command("cd", "//mnt///test/./") == 0);
    CHECK(arch.pcurrent->getCurrentDir() == directory);
    CHECK(fsm.path("message") == directory->find("message"));
    CHECK(!fsm.path("message/") && !fsm.path("message/.."));
    CHECK(!arch.pcurrent->setCurrentDir(directory->find("message")));
    CHECK(!arch.pcurrent->setCurrentDir(nullptr));
    CHECK(command("ls", ".") == 0 && command("pwd") == 0);
    CHECK(command("cat", "message") == 0);
    CHECK(command("cat", "unreadable") != 0);
    CHECK(command("cat", "missing") != 0);
    CHECK(command("cat", ".") != 0);
    CHECK(command("cd", "message") != 0 && command("cd", "missing") != 0);
    CHECK(command("cd", "") != 0);
    CHECK(arch.pcurrent->getCurrentDir() == directory);
    char leaf[32];
    CHECK(fsm.path_parent("new", leaf, sizeof(leaf)) == directory);
    CHECK(!strcmp(leaf, "new"));
    CHECK(fsm.link("message", "relative") == RETURN_OK);
    CHECK(fsm.path("relative") == directory->find("message"));
    CHECK(command("cd", "system/env") == 0);
    CHECK(arch.pcurrent->getCurrentDir() == fsm.path("/sys/env"));
    CHECK(command("pwd") == 0);
    Process *child = new Process(const_cast<char *>("test_child"));
    CHECK(child && child->getCurrentDir() == arch.pcurrent->getCurrentDir());
    delete child;
    CHECK(command("cd", "../..") == 0 && arch.pcurrent->getCurrentDir() == root);
    CHECK(command("cd", "..") == 0 && arch.pcurrent->getCurrentDir() == root);

    File *source = directory->createChild("source", TYPE_DIRECTORY);
    File *destination = directory->createChild("destination", TYPE_DIRECTORY);
    CHECK(source && destination);
    File *current = source->createChild("current", TYPE_DIRECTORY);
    CHECK(current && arch.pcurrent->setCurrentDir(current));
    CHECK(destination->addChild(current) == RETURN_OK);
    char moved_path[64];
    CHECK(fsm.get_path(current, moved_path, sizeof(moved_path)));
    CHECK(!strcmp(moved_path, "/mnt/test/destination/current"));
    CHECK(command("cd", "..") == 0 && arch.pcurrent->getCurrentDir() == destination);
    CHECK(command("cd") == 0 && arch.pcurrent->getCurrentDir() == root);

    char name[91];
    for (u32 i = 0; i < 90; ++i) name[i] = 'd';
    name[90] = 0;
    File *deep = directory;
    for (u32 i = 0; i < 3; ++i) {
        deep = deep->createChild(name, TYPE_DIRECTORY);
        CHECK(deep);
    }
    char path[512];
    CHECK(fsm.get_path(deep, path, sizeof(path)));
    CHECK(command("cd", path) != 0 && arch.pcurrent->getCurrentDir() == root);
    CHECK(command("cd", "/sys") == 0 && command("cd") == 0);
    CHECK(arch.pcurrent->getCurrentDir() == root);
    return true;
}

static bool lifetimes() {
    destroyed = 0;
    File *tree = new File("tree", TYPE_DIRECTORY);
    File *first = new CountedFile("first");
    File *middle = new CountedFile("middle");
    File *last = new CountedFile("last");
    CHECK(tree && first && middle && last);
    CHECK(tree->addChild(first) == RETURN_OK);
    CHECK(tree->addChild(middle) == RETURN_OK);
    CHECK(tree->addChild(last) == RETURN_OK);
    CHECK(tree->addChild(last) == RETURN_OK && last->getNext() == middle);
    CHECK(tree->addChild(tree) != RETURN_OK);
    CHECK(first->addChild(tree) != RETURN_OK);
    delete middle;
    CHECK(last->getNext() == first && first->getPrec() == last);
    delete last;
    CHECK(tree->getChild() == first && !first->getPrec());
    delete first;
    CHECK(!tree->getChild() && destroyed == 3);
    File *left = tree->createChild("left", TYPE_DIRECTORY);
    File *right = tree->createChild("right", TYPE_DIRECTORY);
    File *moved = new CountedFile("moved");
    CHECK(left && right && moved && left->addChild(moved) == RETURN_OK);
    CHECK(right->addChild(moved) == RETURN_OK && !left->getChild());
    CHECK(moved->getParent() == right && left->addChild(tree) != RETURN_OK);
    File *duplicate = new File("moved", TYPE_FILE);
    CHECK(right->addChild(duplicate) != RETURN_OK);
    delete duplicate;
    CHECK(!tree->createChild(".", TYPE_DIRECTORY));
    CHECK(!tree->createChild("..", TYPE_DIRECTORY));
    CHECK(!tree->createChild("bad/name", TYPE_FILE));
    File *replacement = fsm.pivot_root(tree);
    CHECK(replacement && replacement != tree && !tree->getChild());
    CHECK(replacement->find("left") == left && replacement->find("right") == right);
    delete tree;
    delete replacement;
    CHECK(destroyed == 4);
    File *renamed = new File("old_name", TYPE_FILE);
    CHECK(renamed);
    renamed->setName(renamed->getName() + 4);
    CHECK(!strcmp(renamed->getName(), "name"));
    delete renamed;
    for (u32 i = 0; i < 32; ++i) {
        Filesystem temporary;
        temporary.init();
        CHECK(temporary.getRoot()->getChild());
    }
    return true;
}

static bool memory_pressure() {
    unified_allocator.set_policy(ALLOC_POLICY_BUDDY);
    void *head = nullptr;
    u32 count = 0;
    while (void *ptr = kmalloc(32)) {
        *(void **)ptr = head;
        head = ptr;
        CHECK(++count < 8192);
    }
    CHECK(count && !kmalloc(1));
    char leaf[32], path[32];
    for (u32 i = 0; i < 256; ++i) {
        CHECK(fsm.path("dev/../sys/./env") == fsm.path("/sys/env"));
        CHECK(!fsm.path("/missing/child"));
        CHECK(fsm.path_parent("/sys/env/new", leaf, sizeof(leaf)) == fsm.path("/sys/env"));
        CHECK(fsm.get_path(fsm.path("/sys/env"), path, sizeof(path)));
    }
    while (head) {
        void *ptr = head;
        head = *(void **)head;
        kfree(ptr);
    }
    unified_allocator.set_policy(ALLOC_POLICY_SLAB | ALLOC_POLICY_BUDDY |
                                 ALLOC_POLICY_STACK | ALLOC_POLICY_AUTO);
    return true;
}

bool verify_filesystem() {
    File *root = fsm.getRoot();
    CHECK(root && arch.pcurrent && arch.pcurrent->getCurrentDir() == root);
    File *directory = fsm.path("/mnt")->createChild("test", TYPE_DIRECTORY);
    CHECK(directory);
    CHECK(paths(root, directory));
    CHECK(links(directory));
    CHECK(navigation(root, directory));
    CHECK(lifetimes());
    CHECK(memory_pressure());
    CHECK(arch.pcurrent->setCurrentDir(root));
    delete directory;
    CHECK(!fsm.path("/mnt/test"));
    serial::write("[FS] paths, working directories and lifetimes verified\n");
    return true;
}

#ifndef SWAP_H
#define SWAP_H

#include <runtime/types.h>

class File;
struct multiboot_info;
struct page_directory;
struct page_table_entry;

struct swap_device {
    File *file;
    u32 id;
    u32 pages;
    u32 inuse_pages;
    u32 next_slot;
    u16 *references;
    swap_device *next;
};

class SwapManager {
public:
    bool add_file(File *file);
    bool remove_file(const char *name);
    bool swap_out_page(page_directory *pd, u32 address);
    int swap_in_page(page_directory *pd, u32 address, u32 start = 0, u32 end = 0);
    bool retain_slot(u32 slot);
    void release_slot(u32 slot);
    bool reclaim_page(page_directory *protected_pd = nullptr, u32 start = 0, u32 end = 0);

    const swap_device *get_swap_devices() const { return devices; }

private:
    swap_device *devices;

    swap_device *find_device(u32 slot);
    bool reclaim_from(page_directory *pd, u32 start, u32 end, bool cold_only);
};

extern SwapManager swap_manager;
void init_swap(const multiboot_info *boot);

#endif

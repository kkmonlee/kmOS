#ifndef SWAP_H
#define SWAP_H

#include <runtime/types.h>
#include <runtime/list.h>
#include <vmm.h>

#define SWAP_ENTRY_SIZE 4096
#define MAX_SWAP_ENTRIES 65536
#define SWAP_SIGNATURE "SWAPSPACE2"
#define SWAP_HEADER_SIZE 1024

#define SWAP_FLAG_BAD_PAGE 0x40000000
#define SWAP_FLAG_LOCKED   0x80000000

#define SWAP_TYPE_FILE   0
#define SWAP_TYPE_DEVICE 1

struct swap_header {
    char signature[10];
    u32 version;
    u32 last_page;
    u32 nr_badpages;
    u32 padding[125];
    u32 badpages[1];
} __attribute__((packed));

struct swap_entry {
    u32 offset;
    u32 flags;
    struct swap_device *device;
};

struct swap_device {
    u32 type;
    u32 flags;
    u32 pages;
    u32 inuse_pages;
    u32 prio;
    char *path;
    u32 *bitmap;
    u32 bitmap_size;
    struct swap_device *next;
    
    int (*read_page)(struct swap_device *dev, u32 offset, void *buffer);
    int (*write_page)(struct swap_device *dev, u32 offset, void *buffer);
    int (*activate)(struct swap_device *dev);
    int (*deactivate)(struct swap_device *dev);
};

struct page_lru {
    u32 virtual_addr;
    u32 access_time;
    u32 flags;
    struct page_lru *next;
    struct page_lru *prev;
};

struct memory_stats {
    u32 total_pages;
    u32 free_pages;
    u32 used_pages;
    u32 cached_pages;
    u32 swap_total;
    u32 swap_free;
    u32 swap_used;
    u32 pressure_level;
};

#define MEMORY_PRESSURE_NONE   0
#define MEMORY_PRESSURE_LOW    1
#define MEMORY_PRESSURE_MEDIUM 2
#define MEMORY_PRESSURE_HIGH   3
#define MEMORY_PRESSURE_CRITICAL 4

#define SWAP_THRESHOLD_LOW    80
#define SWAP_THRESHOLD_MEDIUM 90
#define SWAP_THRESHOLD_HIGH   95
#define SWAP_THRESHOLD_CRITICAL 98

class SwapManager {
public:
    void init();
    int add_swap_device(const char *path, u32 type, u32 priority);
    int remove_swap_device(const char *path);
    
    u32 swap_out_page(u32 virtual_addr);
    int swap_in_page(u32 virtual_addr, u32 swap_entry);
    
    int get_swap_entry();
    void free_swap_entry(u32 entry);
    
    u32 check_memory_pressure();
    int reclaim_pages(u32 target_pages);
    
    struct page_lru *find_victim_page();
    void update_page_access(u32 virtual_addr);
    void add_to_lru(u32 virtual_addr);
    void remove_from_lru(u32 virtual_addr);
    
    void get_memory_stats(struct memory_stats *stats);
    void print_swap_stats();
    
private:
    struct swap_device *swap_devices;
    u32 total_swap_pages;
    u32 used_swap_pages;
    
    struct page_lru *lru_head;
    struct page_lru *lru_tail;
    u32 lru_count;
    u32 access_counter;
    
    u32 swap_in_count;
    u32 swap_out_count;
    u32 reclaim_attempts;
    
    int allocate_swap_entry(struct swap_device **dev, u32 *offset);
    struct swap_device *find_swap_device(const char *path);
    int validate_swap_device(struct swap_device *dev);
};

extern SwapManager swap_manager;

extern "C" {
    void init_swap_manager();
    int swapon(const char *path, u32 flags);
    int swapoff(const char *path);
    u32 get_memory_pressure();
    int trigger_page_reclaim(u32 pages);
}

#endif
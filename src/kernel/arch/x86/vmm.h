#ifndef VMM_H
#define VMM_H

#include <runtime/types.h>
#include <runtime/list.h>
#include <runtime/alloc.h>
#include <x86.h>
#include <archprocess.h>


struct page_directory_entry {
    u32 present : 1;
    u32 writable : 1;
    u32 user : 1;
    u32 write_through : 1;
    u32 cache_disabled : 1;
    u32 accessed : 1;
    u32 reserved : 1;
    u32 page_size : 1;
    u32 global : 1;
    u32 available : 3;
    u32 frame : 20;
} __attribute__((packed));


struct page_table_entry {
    u32 present : 1;
    u32 writable : 1;
    u32 user : 1;
    u32 write_through : 1;
    u32 cache_disabled : 1;
    u32 accessed : 1;
    u32 dirty : 1;
    u32 pat : 1;
    u32 global : 1;
    u32 swapped : 1;
    u32 cow : 1;
    u32 available : 1;
    u32 frame : 20;
} __attribute__((packed));

struct page_directory {
    struct page_directory_entry tables[1024];
    struct page_table_entry *page_tables[1024];
    u32 physical_address;
};


struct page_frame {
    u32 address;
    u32 ref_count;
    struct page_frame *next;
};


struct multiboot_info;

class VMM {
public:
    void init(const multiboot_info *boot);
    struct page_directory *create_page_directory(bool reclaim = false);
    void destroy_page_directory(struct page_directory *pd);
    int map_page(struct page_directory *pd, u32 virtual_addr, u32 physical_addr, u32 flags);
    void unmap_page(struct page_directory *pd, u32 virtual_addr);
    u32 get_physical_addr(struct page_directory *pd, u32 virtual_addr);
    bool user_range(struct page_directory *pd, u32 address, u32 size, bool writable = false);
    bool user_write_range(struct page_directory *pd, u32 address, u32 size);
    bool prepare_user_write(struct page_directory *pd, u32 address, u32 size, int *error = nullptr);
    bool copy_from_user(struct page_directory *pd, void *buffer, u32 address, u32 size, int *error = nullptr);
    bool copy_to_user(struct page_directory *pd, u32 address, const void *buffer, u32 size, int *error = nullptr);
    int user_string(struct page_directory *pd, u32 address, char *buffer, u32 capacity);
    int handle_page_fault(u32 fault_addr, u32 error_code);
    void switch_page_directory(struct page_directory *pd);
    u32 alloc_frame();
    u32 alloc_user_frame(page_directory *pd = nullptr, u32 start = 0, u32 end = 0);
    void free_frame(u32 frame_addr);
    bool retain_frame(u32 frame_addr);
    u32 frame_references(u32 frame_addr);
    bool verify_page_tables();
    u32 heap_start;
    u32 heap_size;
    page_table_entry *get_page_table(page_directory *pd, u32 address, int create, bool reclaim = false);
    void invalidate_page(page_directory *pd, u32 address);

    u32 frame_count;
    u32 frames_used;
    
private:
    u32 alloc_frames(u32 count);
    bool init_physical_memory(const multiboot_info *boot);
    void update_frame_counts();
};

extern VMM vmm;
extern struct page_directory *kernel_directory;
extern struct page_directory *current_directory;

extern "C" {
    char *get_page_frame();
    void pd0_add_page(char *v_addr, char *p_addr, unsigned int flags);
    void pd_add_page(char *v_addr, char *p_addr, u32 flags, struct page_directory *pd);
    void enable_paging();
    void switch_page_directory_asm(u32 physical_addr);
}

#endif

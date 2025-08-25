#ifndef VMM_H
#define VMM_H

#include <runtime/types.h>
#include <runtime/list.h>
#include <runtime/alloc.h>
#include <x86.h>
#include <archprocess.h>

// Page directory entry structure
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

// Page table entry structure
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
    u32 available : 3;
    u32 frame : 20;
} __attribute__((packed));

// Page directory structure
struct page_directory {
    struct page_directory_entry tables[1024];
    struct page_table_entry *page_tables[1024];
    u32 physical_address;
};

// Physical memory frame management
struct page_frame {
    u32 address;
    u32 ref_count;
    struct page_frame *next;
};

// VMM class for managing virtual memory
class VMM {
public:
    void init();
    struct page_directory *create_page_directory();
    void destroy_page_directory(struct page_directory *pd);
    int map_page(struct page_directory *pd, u32 virtual_addr, u32 physical_addr, u32 flags);
    void unmap_page(struct page_directory *pd, u32 virtual_addr);
    u32 get_physical_addr(struct page_directory *pd, u32 virtual_addr);
    int handle_page_fault(u32 fault_addr, u32 error_code);
    void switch_page_directory(struct page_directory *pd);
    u32 alloc_frame();
    void free_frame(u32 frame_addr);
    struct page_table_entry *get_page_table(struct page_directory *pd, u32 virtual_addr, int create);
    
private:
    struct page_frame *free_frames;
    u32 *frame_bitmap;
    u32 frame_count;
    u32 frames_used;
};

extern VMM vmm;
extern struct page_directory *kernel_directory;
extern struct page_directory *current_directory;

extern "C" {
    char *get_page_frame();
    void pd0_add_page(char *v_addr, char *p_addr, unsigned int flags);
    void pd_add_page(char *v_addr, char *p_addr, u32 flags, struct page_directory *pd);
    void page_fault_handler(regs_t *regs);
    void enable_paging();
    void switch_page_directory_asm(u32 physical_addr);
}

#endif
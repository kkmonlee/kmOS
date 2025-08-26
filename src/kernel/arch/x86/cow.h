#ifndef COW_H
#define COW_H

#include <runtime/types.h>
#include <runtime/list.h>
#include <vmm.h>

#define COW_MAGIC 0xC0BABE1
#define MAX_COW_REFS 65536

struct cow_page {
    u32 physical_addr;
    u32 ref_count;
    u32 magic;
    struct cow_page *next;
    struct cow_page *prev;
};

struct cow_mapping {
    u32 virtual_addr;
    u32 size;
    u32 flags;
    struct page_directory *owner_pd;
    struct cow_page *cow_page;
    struct cow_mapping *next;
};

struct vm_area {
    u32 vm_start;
    u32 vm_end;
    u32 vm_flags;
    u32 vm_pgoff;
    struct vm_area *vm_next;
    struct vm_area *vm_prev;
    struct page_directory *vm_pd;
};

#define VM_READ     0x00000001
#define VM_WRITE    0x00000002  
#define VM_EXEC     0x00000004
#define VM_SHARED   0x00000008
#define VM_MAYREAD  0x00000010
#define VM_MAYWRITE 0x00000020
#define VM_MAYEXEC  0x00000040
#define VM_MAYSHARE 0x00000080
#define VM_GROWSDOWN    0x00000100
#define VM_GROWSUP      0x00000200
#define VM_DENYWRITE    0x00000800

class COWManager {
public:
    void init();
    int handle_cow_fault(struct page_directory *pd, u32 virtual_addr, u32 error_code);
    int cow_copy_page_range(struct page_directory *dst_pd, struct page_directory *src_pd, 
                           u32 start_addr, u32 end_addr);
    void cow_free_page_range(struct page_directory *pd, u32 start_addr, u32 end_addr);
    struct vm_area *create_vma(u32 start, u32 end, u32 flags);
    void destroy_vma(struct vm_area *vma);
    int map_pages(struct page_directory *pd, u32 start_addr, u32 end_addr, u32 flags);
    int unmap_pages(struct page_directory *pd, u32 start_addr, u32 end_addr);
    
    void cleanup_process_cow(struct page_directory *pd);
    void optimize_cow_pages();
    int validate_cow_integrity();
    void print_stats();
    
private:
    struct cow_page *cow_pages;
    struct cow_mapping *cow_mappings;
    u32 total_cow_pages;
    u32 total_cow_mappings;
    
    struct cow_page *alloc_cow_page(u32 physical_addr);
    void free_cow_page(struct cow_page *page);
    struct cow_page *find_cow_page(u32 physical_addr);
    int copy_page_cow(struct page_directory *dst_pd, struct page_directory *src_pd,
                      u32 virtual_addr);
    int break_cow(struct page_directory *pd, u32 virtual_addr);
    void inc_cow_ref(struct cow_page *page);
    void dec_cow_ref(struct cow_page *page);
    
    struct cow_mapping *create_cow_mapping(u32 virtual_addr, u32 size, u32 flags,
                                          struct page_directory *pd, struct cow_page *page);
    void destroy_cow_mapping(struct cow_mapping *mapping);
    struct cow_mapping *find_cow_mapping(struct page_directory *pd, u32 virtual_addr);
};

extern COWManager cow_manager;

extern "C" {
    void init_cow_manager();
    int cow_fork_mm(struct page_directory *child_pd, struct page_directory *parent_pd);
    int cow_handle_page_fault(u32 fault_addr, u32 error_code);
    void cow_cleanup_process(struct page_directory *pd);
    void cow_optimize();
    int cow_validate();
    void cow_print_stats();
}

#endif
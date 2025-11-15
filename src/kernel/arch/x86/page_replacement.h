#ifndef PAGE_REPLACEMENT_H
#define PAGE_REPLACEMENT_H

#include <runtime/types.h>
#include <runtime/list.h>

enum PageReplacementAlgorithm {
    PR_ALGORITHM_LRU = 0,
    PR_ALGORITHM_FIFO,
    PR_ALGORITHM_CLOCK,
    PR_ALGORITHM_LRU_ENHANCED,
    PR_ALGORITHM_COUNT
};

#define PR_FLAG_ACCESSED    0x01
#define PR_FLAG_DIRTY       0x02
#define PR_FLAG_SWAPPED     0x04
#define PR_FLAG_LOCKED      0x08
#define PR_FLAG_ACTIVE      0x10

struct page_descriptor {
    u32 virtual_addr;
    u32 physical_addr;
    u32 flags;
    u32 access_count;
    u64 last_access_time;
    u64 creation_time;
    u32 process_id;
    
    struct page_descriptor *next;
    struct page_descriptor *prev;
    
    union {
        struct {
            u32 lru_position;
        } lru_data;
        
        struct {
            u32 queue_position;
        } fifo_data;
        
        struct {
            u32 clock_hand_passed;
            u32 reference_bit;
        } clock_data;
    };
};

struct replacement_stats {
    u32 total_replacements;
    u32 page_faults;
    u32 hits;
    u32 misses;
    u32 dirty_writebacks;
    u64 total_access_time;
    u32 algorithm_switches;
};

class PageReplacementManager {
public:
    void init();
    void set_algorithm(PageReplacementAlgorithm algorithm);
    PageReplacementAlgorithm get_current_algorithm() const;
    
    struct page_descriptor* find_victim_page();
    void add_page(u32 virtual_addr, u32 physical_addr, u32 flags);
    void remove_page(u32 virtual_addr);
    void update_page_access(u32 virtual_addr);
    void mark_page_dirty(u32 virtual_addr);
    void mark_page_clean(u32 virtual_addr);
    
    void get_replacement_stats(struct replacement_stats *stats);
    void print_algorithm_performance();
    void reset_stats();
    
    int set_memory_pressure_algorithm();
    void tune_algorithm_parameters(u32 param1, u32 param2) { (void)param1; (void)param2; }

private:
    PageReplacementAlgorithm current_algorithm;
    struct page_descriptor *page_list_head;
    struct page_descriptor *page_list_tail;
    u32 total_pages;
    u64 access_counter;
    
    struct replacement_stats stats[PR_ALGORITHM_COUNT];
    struct replacement_stats current_stats;
    

    struct page_descriptor *lru_head;
    struct page_descriptor *lru_tail;
    

    struct page_descriptor *fifo_head;
    struct page_descriptor *fifo_tail;
    

    struct page_descriptor *clock_hand;
    u32 clock_size;
    

    struct page_descriptor* lru_find_victim();
    struct page_descriptor* fifo_find_victim();
    struct page_descriptor* clock_find_victim();
    struct page_descriptor* lru_enhanced_find_victim();
    
    void lru_add_page(struct page_descriptor *page);
    void lru_remove_page(struct page_descriptor *page);
    void lru_update_access(struct page_descriptor *page);
    
    void fifo_add_page(struct page_descriptor *page);
    void fifo_remove_page(struct page_descriptor *page);
    
    void clock_add_page(struct page_descriptor *page);
    void clock_remove_page(struct page_descriptor *page);
    void clock_update_access(struct page_descriptor *page);
    
    struct page_descriptor* find_page(u32 virtual_addr);
    u64 get_system_time();
    u32 calculate_page_score(struct page_descriptor *page);
};

extern PageReplacementManager page_replacement_manager;

extern "C" {
    void init_page_replacement();
    int set_page_replacement_algorithm(u32 algorithm);
    u32 get_page_replacement_stats(void *buffer);
    struct page_descriptor* get_victim_page_for_replacement();
    void notify_page_access(u32 virtual_addr);
    void notify_page_modified(u32 virtual_addr);
}

#endif
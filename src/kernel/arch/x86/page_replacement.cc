#include <os.h>
#include <page_replacement.h>
#include <vmm.h>
#include <runtime/alloc.h>
#include <architecture.h>

extern "C" {
    int strlen(const char *s);
    int strcmp(const char *dst, const char *src);
    void memset(void *ptr, int value, int num);
}

PageReplacementManager page_replacement_manager;

static const char* algorithm_names[] = {
    "LRU", "FIFO", "Clock", "Enhanced LRU"
};

// Get system memory pressure level (0-100)
static u32 get_memory_pressure() {
    extern VMM vmm;
    
    if (vmm.frame_count == 0) {
        return 0;
    }
    
    // Calculate pressure percentage based on used frames
    return (vmm.frames_used * 100) / vmm.frame_count;
}

void PageReplacementManager::init() {
    io.print("[PAGE_REPL] Initializing page replacement manager\n");
    
    current_algorithm = PR_ALGORITHM_LRU;
    page_list_head = nullptr;
    page_list_tail = nullptr;
    total_pages = 0;
    access_counter = 0;
    
    // Initialize algorithm-specific structures
    lru_head = nullptr;
    lru_tail = nullptr;
    fifo_head = nullptr;
    fifo_tail = nullptr;
    clock_hand = nullptr;
    clock_size = 0;
    
    // Clear all statistics
    for (int i = 0; i < PR_ALGORITHM_COUNT; i++) {
        memset(&stats[i], 0, sizeof(struct replacement_stats));
    }
    memset(&current_stats, 0, sizeof(struct replacement_stats));
    
    io.print("[PAGE_REPL] Page replacement manager initialized with %s algorithm\n", 
             algorithm_names[current_algorithm]);
}

void PageReplacementManager::set_algorithm(PageReplacementAlgorithm algorithm) {
    if (algorithm >= PR_ALGORITHM_COUNT) {
        io.print("[PAGE_REPL] Invalid algorithm %d\n", algorithm);
        return;
    }
    
    io.print("[PAGE_REPL] Switching from %s to %s algorithm\n", 
             algorithm_names[current_algorithm], algorithm_names[algorithm]);
    
    // Save current stats
    stats[current_algorithm] = current_stats;
    current_stats.algorithm_switches++;
    
    current_algorithm = algorithm;
    
    // Reorganize existing pages for new algorithm
    struct page_descriptor *page = page_list_head;
    while (page) {
        struct page_descriptor *next = page->next;
        
        // Remove from old algorithm structures
        switch (stats[current_algorithm].algorithm_switches > 0 ? 
               (PageReplacementAlgorithm)((current_algorithm + PR_ALGORITHM_COUNT - 1) % PR_ALGORITHM_COUNT) : 
               current_algorithm) {
            case PR_ALGORITHM_LRU:
            case PR_ALGORITHM_LRU_ENHANCED:
                lru_remove_page(page);
                break;
            case PR_ALGORITHM_FIFO:
                fifo_remove_page(page);
                break;
            case PR_ALGORITHM_CLOCK:
                clock_remove_page(page);
                break;
            default:
                break;
        }
        
        // Add to new algorithm structures
        switch (current_algorithm) {
            case PR_ALGORITHM_LRU:
            case PR_ALGORITHM_LRU_ENHANCED:
                lru_add_page(page);
                break;
            case PR_ALGORITHM_FIFO:
                fifo_add_page(page);
                break;
            case PR_ALGORITHM_CLOCK:
                clock_add_page(page);
                break;
            default:
                break;
        }
        
        page = next;
    }
}

PageReplacementAlgorithm PageReplacementManager::get_current_algorithm() const {
    return current_algorithm;
}

struct page_descriptor* PageReplacementManager::find_victim_page() {
    if (total_pages == 0) {
        return nullptr;
    }
    
    struct page_descriptor *victim = nullptr;
    u64 start_time = get_system_time();
    
    switch (current_algorithm) {
        case PR_ALGORITHM_LRU:
            victim = lru_find_victim();
            break;
        case PR_ALGORITHM_FIFO:
            victim = fifo_find_victim();
            break;
        case PR_ALGORITHM_CLOCK:
            victim = clock_find_victim();
            break;
        case PR_ALGORITHM_LRU_ENHANCED:
            victim = lru_enhanced_find_victim();
            break;
        default:
            victim = lru_find_victim();
            break;
    }
    
    if (victim) {
        current_stats.total_replacements++;
        current_stats.total_access_time += get_system_time() - start_time;
        
        if (victim->flags & PR_FLAG_DIRTY) {
            current_stats.dirty_writebacks++;
        }
    }
    
    return victim;
}

void PageReplacementManager::add_page(u32 virtual_addr, u32 physical_addr, u32 flags) {
    struct page_descriptor *page = (struct page_descriptor *)kmalloc(sizeof(struct page_descriptor));
    if (!page) {
        io.print("[PAGE_REPL] Failed to allocate page descriptor\n");
        return;
    }
    
    page->virtual_addr = virtual_addr;
    page->physical_addr = physical_addr;
    page->flags = flags;
    page->access_count = 1;
    page->last_access_time = get_system_time();
    page->creation_time = page->last_access_time;
    
    // Get actual process ID from architecture
    page->process_id = (arch.pcurrent != nullptr) ? arch.pcurrent->getPid() : 0;
    page->next = nullptr;
    page->prev = nullptr;
    
    // Initialize algorithm-specific data
    page->lru_data.lru_position = 0;
    page->fifo_data.queue_position = 0;
    page->clock_data.clock_hand_passed = 0;
    page->clock_data.reference_bit = 1;
    
    // Add to general page list
    if (page_list_tail) {
        page_list_tail->next = page;
        page->prev = page_list_tail;
        page_list_tail = page;
    } else {
        page_list_head = page_list_tail = page;
    }
    
    // Add to algorithm-specific structures
    switch (current_algorithm) {
        case PR_ALGORITHM_LRU:
        case PR_ALGORITHM_LRU_ENHANCED:
            lru_add_page(page);
            break;
        case PR_ALGORITHM_FIFO:
            fifo_add_page(page);
            break;
        case PR_ALGORITHM_CLOCK:
            clock_add_page(page);
            break;
        default:
            break;
    }
    
    total_pages++;
}

void PageReplacementManager::remove_page(u32 virtual_addr) {
    struct page_descriptor *page = find_page(virtual_addr);
    if (!page) {
        return;
    }
    
    // Remove from algorithm-specific structures
    switch (current_algorithm) {
        case PR_ALGORITHM_LRU:
        case PR_ALGORITHM_LRU_ENHANCED:
            lru_remove_page(page);
            break;
        case PR_ALGORITHM_FIFO:
            fifo_remove_page(page);
            break;
        case PR_ALGORITHM_CLOCK:
            clock_remove_page(page);
            break;
        default:
            break;
    }
    
    // Remove from general list
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        page_list_head = page->next;
    }
    
    if (page->next) {
        page->next->prev = page->prev;
    } else {
        page_list_tail = page->prev;
    }
    
    kfree(page);
    total_pages--;
}

void PageReplacementManager::update_page_access(u32 virtual_addr) {
    struct page_descriptor *page = find_page(virtual_addr);
    if (!page) {
        current_stats.misses++;
        return;
    }
    
    current_stats.hits++;
    page->access_count++;
    page->last_access_time = get_system_time();
    page->flags |= PR_FLAG_ACCESSED;
    
    switch (current_algorithm) {
        case PR_ALGORITHM_LRU:
        case PR_ALGORITHM_LRU_ENHANCED:
            lru_update_access(page);
            break;
        case PR_ALGORITHM_CLOCK:
            clock_update_access(page);
            break;
        case PR_ALGORITHM_FIFO:
            // FIFO doesn't update on access
            break;
        default:
            break;
    }
}

// LRU Algorithm Implementation
struct page_descriptor* PageReplacementManager::lru_find_victim() {
    if (!lru_tail) {
        return nullptr;
    }
    
    // Find the least recently used page (at tail)
    struct page_descriptor *victim = lru_tail;
    
    // Skip locked pages
    while (victim && (victim->flags & PR_FLAG_LOCKED)) {
        victim = victim->prev;
    }
    
    return victim;
}

void PageReplacementManager::lru_add_page(struct page_descriptor *page) {
    page->lru_data.lru_position = access_counter++;
    
    if (lru_head) {
        lru_head->prev = page;
        page->next = lru_head;
        lru_head = page;
    } else {
        lru_head = lru_tail = page;
    }
}

void PageReplacementManager::lru_remove_page(struct page_descriptor *page) {
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        lru_head = page->next;
    }
    
    if (page->next) {
        page->next->prev = page->prev;
    } else {
        lru_tail = page->prev;
    }
}

void PageReplacementManager::lru_update_access(struct page_descriptor *page) {
    // Remove from current position
    lru_remove_page(page);
    
    // Add to head (most recently used)
    lru_add_page(page);
}

// FIFO Algorithm Implementation
struct page_descriptor* PageReplacementManager::fifo_find_victim() {
    if (!fifo_tail) {
        return nullptr;
    }
    
    struct page_descriptor *victim = fifo_tail;
    
    // Skip locked pages
    while (victim && (victim->flags & PR_FLAG_LOCKED)) {
        victim = victim->prev;
    }
    
    return victim;
}

void PageReplacementManager::fifo_add_page(struct page_descriptor *page) {
    page->fifo_data.queue_position = access_counter++;
    
    if (fifo_head) {
        fifo_head->prev = page;
        page->next = fifo_head;
        fifo_head = page;
    } else {
        fifo_head = fifo_tail = page;
    }
}

void PageReplacementManager::fifo_remove_page(struct page_descriptor *page) {
    if (page->prev) {
        page->prev->next = page->next;
    } else {
        fifo_head = page->next;
    }
    
    if (page->next) {
        page->next->prev = page->prev;
    } else {
        fifo_tail = page->prev;
    }
}

// Clock Algorithm Implementation
struct page_descriptor* PageReplacementManager::clock_find_victim() {
    if (clock_size == 0) {
        return nullptr;
    }
    
    struct page_descriptor *start = clock_hand;
    if (!start) {
        start = clock_hand = page_list_head;
    }
    
    // Clock algorithm: look for page with reference bit = 0
    do {
        if (!(clock_hand->flags & PR_FLAG_LOCKED)) {
            if (clock_hand->clock_data.reference_bit == 0) {
                // Found victim
                struct page_descriptor *victim = clock_hand;
                clock_hand = clock_hand->next ? clock_hand->next : page_list_head;
                return victim;
            } else {
                // Give second chance, clear reference bit
                clock_hand->clock_data.reference_bit = 0;
                clock_hand->clock_data.clock_hand_passed++;
            }
        }
        
        clock_hand = clock_hand->next ? clock_hand->next : page_list_head;
    } while (clock_hand != start);
    
    // If all pages are locked, return the current hand position
    return clock_hand;
}

void PageReplacementManager::clock_add_page(struct page_descriptor *page) {
    page->clock_data.reference_bit = 1;
    page->clock_data.clock_hand_passed = 0;
    clock_size++;
    
    if (!clock_hand) {
        clock_hand = page;
    }
}

void PageReplacementManager::clock_remove_page(struct page_descriptor *page) {
    if (clock_hand == page) {
        clock_hand = page->next ? page->next : page_list_head;
        if (clock_hand == page) {
            clock_hand = nullptr;
        }
    }
    clock_size--;
}

void PageReplacementManager::clock_update_access(struct page_descriptor *page) {
    page->clock_data.reference_bit = 1;
}

// Enhanced LRU Algorithm Implementation
struct page_descriptor* PageReplacementManager::lru_enhanced_find_victim() {
    u32 best_score = 0xFFFFFFFF;
    struct page_descriptor *best_victim = nullptr;
    
    // Enhanced LRU considers multiple factors:
    // - Access frequency
    // - Time since last access
    // - Dirty bit (prefer clean pages)
    // - Page age
    
    struct page_descriptor *page = page_list_head;
    while (page) {
        if (!(page->flags & PR_FLAG_LOCKED)) {
            u32 score = calculate_page_score(page);
            if (score < best_score) {
                best_score = score;
                best_victim = page;
            }
        }
        page = page->next;
    }
    
    return best_victim;
}

u32 PageReplacementManager::calculate_page_score(struct page_descriptor *page) {
    u64 current_time = get_system_time();
    u32 score = 0;
    
    // Time factor (higher = older)
    u32 age = (u32)(current_time - page->last_access_time);
    score += age >> 10; // Divide by 1024
    
    // Access frequency factor (lower frequency = higher score)
    if (page->access_count > 0) {
        score += (1000 / page->access_count);
    } else {
        score += 1000;
    }
    
    // Dirty page penalty (prefer clean pages)
    if (page->flags & PR_FLAG_DIRTY) {
        score += 500;
    }
    
    // Active page bonus (recently accessed pages get lower score)
    if (page->flags & PR_FLAG_ACCESSED) {
        score -= 100;
    }
    
    return score;
}

struct page_descriptor* PageReplacementManager::find_page(u32 virtual_addr) {
    struct page_descriptor *page = page_list_head;
    while (page) {
        if (page->virtual_addr == virtual_addr) {
            return page;
        }
        page = page->next;
    }
    return nullptr;
}

u64 PageReplacementManager::get_system_time() {
    return access_counter++; // Simplified time counter
}

void PageReplacementManager::mark_page_dirty(u32 virtual_addr) {
    struct page_descriptor *page = find_page(virtual_addr);
    if (page) {
        page->flags |= PR_FLAG_DIRTY;
    }
}

void PageReplacementManager::mark_page_clean(u32 virtual_addr) {
    struct page_descriptor *page = find_page(virtual_addr);
    if (page) {
        page->flags &= ~PR_FLAG_DIRTY;
    }
}

void PageReplacementManager::get_replacement_stats(struct replacement_stats *stats_out) {
    *stats_out = current_stats;
}

void PageReplacementManager::print_algorithm_performance() {
    io.print("\n[PAGE_REPL] === Page Replacement Statistics ===\n");
    io.print("[PAGE_REPL] Current Algorithm: %s\n", algorithm_names[current_algorithm]);
    io.print("[PAGE_REPL] Total Pages: %d\n", total_pages);
    io.print("[PAGE_REPL] Total Replacements: %d\n", current_stats.total_replacements);
    io.print("[PAGE_REPL] Page Faults: %d\n", current_stats.page_faults);
    io.print("[PAGE_REPL] Hits: %d, Misses: %d\n", current_stats.hits, current_stats.misses);
    io.print("[PAGE_REPL] Dirty Writebacks: %d\n", current_stats.dirty_writebacks);
    io.print("[PAGE_REPL] Algorithm Switches: %d\n", current_stats.algorithm_switches);
    
    if (current_stats.hits + current_stats.misses > 0) {
        u32 hit_rate = (current_stats.hits * 100) / (current_stats.hits + current_stats.misses);
        io.print("[PAGE_REPL] Hit Rate: %d%%\n", hit_rate);
    }
    
    io.print("\n[PAGE_REPL] Algorithm Performance Summary:\n");
    for (int i = 0; i < PR_ALGORITHM_COUNT; i++) {
        if (stats[i].total_replacements > 0) {
            io.print("[PAGE_REPL] %s: %d replacements, %d dirty writebacks\n",
                     algorithm_names[i], stats[i].total_replacements, stats[i].dirty_writebacks);
        }
    }
}

void PageReplacementManager::reset_stats() {
    memset(&current_stats, 0, sizeof(struct replacement_stats));
}

int PageReplacementManager::set_memory_pressure_algorithm() {
    // Automatically choose algorithm based on actual memory pressure from VMM
    u32 pressure = get_memory_pressure();
    
    if (pressure < 50) {
        // Low pressure: use sophisticated LRU
        set_algorithm(PR_ALGORITHM_LRU);
        return 0;
    } else if (pressure < 80) {
        // Medium pressure: use enhanced LRU with aging
        set_algorithm(PR_ALGORITHM_LRU_ENHANCED);
        return 1;
    } else if (pressure < 95) {
        // High pressure: use faster Clock algorithm
        set_algorithm(PR_ALGORITHM_CLOCK);
        return 2;
    } else {
        // Critical pressure: use simplest FIFO
        set_algorithm(PR_ALGORITHM_FIFO);
        return 3;
    }
}

// C interface functions
extern "C" {
    void init_page_replacement() {
        page_replacement_manager.init();
    }
    
    int set_page_replacement_algorithm(u32 algorithm) {
        if (algorithm >= PR_ALGORITHM_COUNT) {
            return -1;
        }
        page_replacement_manager.set_algorithm((PageReplacementAlgorithm)algorithm);
        return 0;
    }
    
    u32 get_page_replacement_stats(void *buffer) {
        struct replacement_stats *stats = (struct replacement_stats *)buffer;
        page_replacement_manager.get_replacement_stats(stats);
        return sizeof(struct replacement_stats);
    }
    
    struct page_descriptor* get_victim_page_for_replacement() {
        return page_replacement_manager.find_victim_page();
    }
    
    void notify_page_access(u32 virtual_addr) {
        page_replacement_manager.update_page_access(virtual_addr);
    }
    
    void notify_page_modified(u32 virtual_addr) {
        page_replacement_manager.mark_page_dirty(virtual_addr);
    }
}
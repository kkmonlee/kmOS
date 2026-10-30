#include <os.h>
#include <architecture.h>
#include <vmm.h>
#include <pit.h>
#include <runtime/alloc.h>
#include <elf_loader.h>

extern "C" {
    void *memset(void *s, int c, int n);
    u64 get_system_ticks();
    extern char boot_stack_top;
}

extern VMM vmm;
extern PIT pit;
extern IO io;
extern struct page_directory *current_directory;

static void serial_outb(unsigned short port, unsigned char data)
{
    asm volatile("outb %0, %1" : : "a"(data), "Nd"(port));
}

static void serial_print(const char *str)
{
    while (*str)
    {
        serial_outb(0x3F8, *str);
        str++;
    }
}

static void idle_thread()
{
    while (1)
    {
        asm volatile("hlt");
    }
}

void Architecture::init(const multiboot_info *boot)
{
    serial_print("[ARCH] Starting x86 architecture initialization\n");
    io.print("[ARCH] Initializing x86 architecture\n");

    serial_print("[ARCH] About to initialize VMM\n");
    vmm.init(boot);

    plist = nullptr;
    pcurrent = nullptr;
    firstProc = nullptr;
    idle_process = nullptr;
    ticks_since_switch = 0;
    ticks_per_quantum = 1;
    total_ticks = 0;
    scheduler_ready = false;

    configure_scheduler(PIT_DEFAULT_FREQ);

    serial_print("[ARCH] VMM initialized (paging handled by VMM)\n");
    io.print("[ARCH] x86 architecture initialization complete\n");
}

void Architecture::configure_scheduler(u32 pit_frequency_hz)
{
    if (pit_frequency_hz == 0)
    {
        pit_frequency_hz = PIT_DEFAULT_FREQ;
    }

    const u32 desired_quantum_ms = 20;
    u32 ticks = (pit_frequency_hz * desired_quantum_ms) / 1000;
    if (ticks == 0)
    {
        ticks = 1;
    }

    ticks_per_quantum = ticks;
    scheduler_ready = true;
}

void Architecture::enqueue_process(Process *proc, bool make_current)
{
    if (proc == nullptr)
    {
        return;
    }

    if (plist == nullptr)
    {
        plist = proc;
        firstProc = proc;
        proc->setPNext(proc);
    }
    else
    {
        proc->setPNext(plist);
        firstProc->setPNext(proc);
        firstProc = proc;
    }

    if (make_current || pcurrent == nullptr)
    {
        if (pcurrent != nullptr && pcurrent != proc && pcurrent->getState() == RUNNING)
        {
            pcurrent->setState(READY);
        }

        pcurrent = proc;
        proc->setState(RUNNING);
        proc->last_scheduled = get_system_ticks();
        proc->time_slice = 0;
        ticks_since_switch = 0;
    }
    else
    {
        proc->setState(READY);
    }
}

void Architecture::ensure_idle_process()
{
    if (idle_process != nullptr)
    {
        return;
    }

    Process *idle = new Process(const_cast<char *>("idle"));
    idle_process = idle;

    process_st *info = idle->getPInfo();
    memset(info, 0, sizeof(process_st));

    const u32 stack_size = 4096;
    u8 *stack = (u8 *)kmalloc(stack_size);
    if (stack == nullptr)
    {
        io.print("[ARCH] Failed to allocate idle stack\n");
        return;
    }

    u32 stack_top = (u32)(stack + stack_size);

    info->pd = current_directory;
    info->regs.eip = (u32)&idle_thread;
    info->regs.esp = stack_top;
    info->regs.ebp = stack_top;
    info->regs.eflags = 0x202;
    info->regs.cs = 0x08;
    info->regs.ss = 0x10;
    info->regs.ds = 0x10;
    info->regs.es = 0x10;
    info->regs.fs = 0x10;
    info->regs.gs = 0x10;
    info->regs.cr3 = current_directory ? current_directory->physical_address : 0;
    info->vinfo = stack;
    info->kernel_stack_top = stack_top;

    idle->setPParent(nullptr);
    idle->setState(READY);
    idle->last_scheduled = get_system_ticks();
    idle->time_slice = 0;
    idle->total_runtime = 0;

    u32 interrupt_flags = irq_save();
    enqueue_process(idle, false);
    irq_restore(interrupt_flags);
}

void Architecture::addProcess(Process *p)
{
    if (p == nullptr)
    {
        return;
    }

    u32 interrupt_flags = irq_save();
    bool list_was_empty = (plist == nullptr);
    enqueue_process(p, list_was_empty);
    irq_restore(interrupt_flags);

    if (list_was_empty)
    {
        scheduler_ready = true;
    }

    if (idle_process == nullptr)
    {
        ensure_idle_process();
    }
}

void Architecture::enable_interrupt()
{
    asm volatile("sti");
}

void Architecture::disable_interrupt()
{
    asm volatile("cli");
}

char *Architecture::detect()
{
    return (char *)"Generic x86";
}

u32 Architecture::getArg(u32 argnum)
{
    if (!pcurrent || !pcurrent->getPInfo()->syscall_frame) return 0;
    const InterruptFrame *frame = (const InterruptFrame *)pcurrent->getPInfo()->syscall_frame;
    switch (argnum) {
    case 0: return frame->ebx;
    case 1: return frame->ecx;
    case 2: return frame->edx;
    case 3: return frame->esi;
    case 4: return frame->edi;
    case 5: return frame->ebp;
    default: return 0;
    }
}

int Architecture::createProc(process_st *info, const void *file, u32 size, int argc, const char *const *argv)
{
    if (!info || info->vinfo || !load_elf(file, size, info, argc, argv)) return 0;
    u8 *stack = (u8 *)kmalloc(KERNEL_STACK_SIZE);
    if (!stack) {
        vmm.destroy_page_directory(info->pd);
        memset(info, 0, sizeof(*info));
        return 0;
    }
    info->vinfo = stack;
    info->kernel_stack_top = (u32)(stack + KERNEL_STACK_SIZE);
    info->interrupt_stack = info->kernel_stack_top - sizeof(InterruptFrame);
    memset((void *)info->interrupt_stack, 0, sizeof(InterruptFrame));
    info->regs.cs = 0x23;
    info->regs.ss = 0x2b;
    info->regs.ds = 0x2b;
    info->regs.es = 0x2b;
    info->regs.fs = 0x2b;
    info->regs.gs = 0x2b;
    info->regs.eflags = 0x202;
    info->regs.cr3 = info->pd->physical_address;
    return 1;
}

void Architecture::setRet(u32 value)
{
    if (pcurrent && pcurrent->getPInfo()->syscall_frame)
        ((InterruptFrame *)pcurrent->getPInfo()->syscall_frame)->eax = value;
}

void Architecture::change_process_father(Process *child, Process *new_father)
{
    if (child == nullptr)
    {
        return;
    }

    u32 interrupt_flags = irq_save();
    Process *cursor = plist;
    if (cursor != nullptr)
    {
        do
        {
            if (cursor->getPParent() == child)
            {
                cursor->setPParent(new_father);
            }
            cursor = cursor->getPNext();
        } while (cursor != nullptr && cursor != plist);
    }
    irq_restore(interrupt_flags);
}

Process *Architecture::find_previous(Process *target)
{
    if (plist == nullptr || target == nullptr)
    {
        return nullptr;
    }

    Process *cursor = plist;
    Process *prev = nullptr;

    do
    {
        if (cursor == target)
        {
            if (prev != nullptr)
            {
                return prev;
            }

            Process *tail = cursor;
            while (tail->getPNext() != plist)
            {
                tail = tail->getPNext();
            }
            return tail;
        }

        prev = cursor;
        cursor = cursor->getPNext();
    } while (cursor != nullptr && cursor != plist);

    return nullptr;
}

void Architecture::remove_process(Process *proc)
{
    if (plist == nullptr || proc == nullptr)
    {
        return;
    }

    Process *next = proc->getPNext();

    if (proc == plist && proc->getPNext() == proc)
    {
        plist = nullptr;
        firstProc = nullptr;
        if (pcurrent == proc)
        {
            pcurrent = nullptr;
        }
        proc->setPNext(nullptr);
        return;
    }

    Process *prev = find_previous(proc);
    if (prev == nullptr)
    {
        return;
    }

    prev->setPNext(next);

    if (plist == proc)
    {
        plist = next;
    }

    if (firstProc == proc)
    {
        firstProc = prev;
    }

    if (pcurrent == proc)
    {
        pcurrent = next;
    }

    proc->setPNext(nullptr);
}

void Architecture::destroy_process(Process *proc)
{
    if (proc == nullptr || proc == idle_process)
    {
        return;
    }

    u32 interrupt_flags = irq_save();
    if (proc == pcurrent) {
        irq_restore(interrupt_flags);
        return;
    }
    remove_process(proc);
    irq_restore(interrupt_flags);

    delete proc;
}

struct page_directory *Architecture::get_current_page_directory()
{
    return current_directory;
}

void Architecture::destroy_all_zombie()
{
    u32 interrupt_flags = irq_save();

    if (plist == nullptr)
    {
        irq_restore(interrupt_flags);
        return;
    }

    Process *cursor = plist;
    Process *pending_cleanup = nullptr;

    do
    {
        Process *next = cursor->getPNext();
        if (cursor != idle_process && cursor != pcurrent && cursor->getState() == ZOMBIE)
        {
            remove_process(cursor);
            cursor->setPNext(pending_cleanup);
            pending_cleanup = cursor;

            if (plist == nullptr)
            {
                break;
            }
        }

        cursor = next;
    } while (cursor != nullptr && cursor != plist);

    irq_restore(interrupt_flags);

    while (pending_cleanup != nullptr)
    {
        Process *victim = pending_cleanup;
        pending_cleanup = pending_cleanup->getPNext();
        victim->setPNext(nullptr);
        delete victim;
    }
}

void Architecture::save_process_context(Process *proc, const InterruptFrame *frame)
{
    if (proc == nullptr || frame == nullptr)
    {
        return;
    }

    process_st *info = proc->getPInfo();

    info->regs.eax = frame->eax;
    info->regs.ecx = frame->ecx;
    info->regs.edx = frame->edx;
    info->regs.ebx = frame->ebx;
    info->interrupt_stack = (u32)frame;
    info->regs.esp = (frame->cs & 3) ? frame->useresp : (u32)frame + __builtin_offsetof(InterruptFrame, useresp);
    info->regs.ebp = frame->ebp;
    info->regs.esi = frame->esi;
    info->regs.edi = frame->edi;
    info->regs.eip = frame->eip;
    info->regs.eflags = frame->eflags;
    info->regs.cs = frame->cs & 0xFFFF;
    info->regs.ds = frame->ds & 0xFFFF;
    info->regs.es = frame->es & 0xFFFF;
    info->regs.fs = frame->fs & 0xFFFF;
    info->regs.gs = frame->gs & 0xFFFF;
    info->regs.ss = (frame->cs & 3) ? frame->ss : 0x10;

    if (current_directory != nullptr)
    {
        info->regs.cr3 = current_directory->physical_address;
    }
}

InterruptFrame *Architecture::load_process_context(Process *proc)
{
    process_st *info = proc->getPInfo();
    InterruptFrame *frame = (InterruptFrame *)(info->interrupt_stack ? info->interrupt_stack :
        info->regs.esp - __builtin_offsetof(InterruptFrame, useresp));

    frame->eax = info->regs.eax;
    frame->ecx = info->regs.ecx;
    frame->edx = info->regs.edx;
    frame->ebx = info->regs.ebx;
    frame->esp = info->regs.esp;
    frame->ebp = info->regs.ebp;
    frame->esi = info->regs.esi;
    frame->edi = info->regs.edi;
    frame->eip = info->regs.eip;

    u32 data_selector = (info->regs.cs & 3) ? info->regs.ds : (info->regs.ds ? info->regs.ds : 0x10);
    frame->cs = info->regs.cs ? info->regs.cs : 0x08;
    frame->ds = data_selector;
    frame->es = (info->regs.cs & 3) ? info->regs.es : data_selector;
    frame->fs = (info->regs.cs & 3) ? info->regs.fs : data_selector;
    frame->gs = (info->regs.cs & 3) ? info->regs.gs : data_selector;
    if (info->regs.cs & 3) {
        frame->ss = info->regs.ss;
        frame->useresp = info->regs.esp;
    }
    frame->eflags = (info->regs.eflags ? info->regs.eflags : 0x202) | 0x200;
    default_tss.esp0 = info->kernel_stack_top ? info->kernel_stack_top : (u32)&boot_stack_top;

    if (info->pd != nullptr && info->pd != current_directory)
    {
        vmm.switch_page_directory(info->pd);
    }
    return frame;
}

InterruptFrame *Architecture::handle_timer_interrupt(InterruptFrame *frame)
{
    total_ticks++;

    if (!scheduler_ready || pcurrent == nullptr)
    {
        return frame;
    }

    save_process_context(pcurrent, frame);

    ticks_since_switch++;

    bool need_switch = false;

    if (pcurrent->getState() != RUNNING)
    {
        need_switch = true;
    }

    if (ticks_since_switch >= ticks_per_quantum)
    {
        need_switch = true;
    }

    if (!need_switch)
    {
        return frame;
    }

    ticks_since_switch = 0;

    Process *next = pcurrent->schedule();
    if (next == nullptr || next == pcurrent) return frame;

    pcurrent = next;
    return load_process_context(pcurrent);
}

InterruptFrame *Architecture::finish_process(int status) {
    pcurrent->exit(status);
    Process *next = pcurrent->schedule();
    pcurrent = next;
    ticks_since_switch = 0;
    return load_process_context(next);
}

int Architecture::fork(process_st *info, process_st *father) {
    if (!info || !info->pd || !father || !father->syscall_frame) return 0;
    const InterruptFrame *frame = (const InterruptFrame *)father->syscall_frame;
    if ((frame->cs & 3) != 3) return 0;
    u8 *stack = (u8 *)kmalloc(KERNEL_STACK_SIZE);
    if (!stack) return 0;
    info->vinfo = stack;
    info->kernel_stack_top = (u32)(stack + KERNEL_STACK_SIZE);
    info->interrupt_stack = info->kernel_stack_top - sizeof(InterruptFrame);
    info->regs.eax = 0;
    info->regs.ecx = frame->ecx;
    info->regs.edx = frame->edx;
    info->regs.ebx = frame->ebx;
    info->regs.esp = frame->useresp;
    info->regs.ebp = frame->ebp;
    info->regs.esi = frame->esi;
    info->regs.edi = frame->edi;
    info->regs.eip = frame->eip;
    info->regs.eflags = frame->eflags;
    info->regs.cs = frame->cs;
    info->regs.ss = frame->ss;
    info->regs.ds = frame->ds;
    info->regs.es = frame->es;
    info->regs.fs = frame->fs;
    info->regs.gs = frame->gs;
    info->regs.cr3 = info->pd->physical_address;
    info->b_exec = father->b_exec;
    info->e_exec = father->e_exec;
    info->b_bss = father->b_bss;
    info->e_bss = father->e_bss;
    info->b_heap = father->b_heap;
    info->e_heap = father->e_heap;
    return 1;
}

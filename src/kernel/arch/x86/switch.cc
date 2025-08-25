#include <os.h>

extern "C" void switch_to_task(struct process_st * /*current*/, int /*mode*/) {
}

extern "C" void __switch_to_task(struct process_st * /*current*/, int /*mode*/) {
}
#ifndef VMM_H
#define VMM_H

#include <runtime/types.h>
#include <runtime/list.h>
#include <runtime/alloc.h>
#include <x86.h>
#include <archprocess.h>

extern "C" {
    char *get_page_frame();
    void pd0_add_page(char *v_addr, char *p_addr, unsigned int flags);
}

#endif
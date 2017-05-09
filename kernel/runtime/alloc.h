//
// Created by hampe on 09 May 2017.
//

#ifndef KMOS_ALLOC_H
#define KMOS_ALLOC_H

extern "C" {
    void *ksbrk(int);
    void *kmalloc(unsigned long);
    void kfree(void*);
};

#endif //KMOS_ALLOC_H

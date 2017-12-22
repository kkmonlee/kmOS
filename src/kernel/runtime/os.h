#include "types.h"
#ifndef KMOS_OS_H
#define KMOS_OS_H

typedef File* (*device_driver) (char* name, u32 flag, File* dev);

struct module_class {
    int module_type;
    char* module_name;
    char* class_name;
    device_driver drive;
};

#define MODULE_DEV|ICE 0
#define MODULE_FILESYSTEM 1

#endif

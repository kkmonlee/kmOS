#ifndef OS_H
#define OS_H

#include <config.h>
#include <kernel.h>

typedef File *(*device_driver)(char *name, u32 flag, File *dev);

struct module_class
{
  int module_type;
  char *module_name;
  char *class_name;
  device_driver driver;
};

#define MODULE_DEVICE 0
#define MODULE_FILESYSTEM 1
#define module(name, type, class, mknod) module_class clazz##_module = {type, name, class, mknod};

#define import_module(clazz) extern module_class clazz##_module;
#define run_module_builder module_class *module_builder[];
#define end_module() NULL

#define std_builtin_module void Module::init()
#define run_module(n, m, f) createDevice(#m, #n, f);

#define asm __asm__
#define asmv __asm__ __volatile__

#endif // OS_H
#ifndef CONFIG_H
#define CONFIG_H

#define KERNEL_NAME         "kmos"
#define KERNEL_VERSION      "1"
#define KERNEL_DATE         __DATE__
#define KERNEL_TIME         __TIME__
#define KERNEL_LICENCE      "Apache License"
#define KERNEL_COMPUTERNAME "Envy"

#ifdef __x86__
#define KERNEL_PROCESSOR_IDENTIFIER "x86"
#else
#define KERNEL_PROCESSOR_IDENTIFIER "(null)"
#endif

#define CONFIG_MAX_FILE 32

#endif
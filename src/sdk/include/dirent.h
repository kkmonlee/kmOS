#ifndef KMOS_DIRENT_H
#define KMOS_DIRENT_H

#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_REG 8
#define DT_LNK 10

struct dirent {
    unsigned int d_ino;
    unsigned int d_type;
    char d_name[256];
};

#endif

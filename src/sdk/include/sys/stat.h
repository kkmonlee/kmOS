#ifndef KMOS_SYS_STAT_H
#define KMOS_SYS_STAT_H

#define S_IFMT 0170000
#define S_IFIFO 0010000
#define S_IFCHR 0020000
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define S_IFLNK 0120000

#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)

struct stat {
    unsigned int st_dev;
    unsigned int st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned int st_size;
    unsigned int st_blksize;
    unsigned int st_blocks;
    unsigned int st_atime;
    unsigned int st_mtime;
    unsigned int st_ctime;
};

#endif

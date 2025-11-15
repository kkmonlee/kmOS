#ifndef FILESYSTEM_DRIVER_H
#define FILESYSTEM_DRIVER_H

#include <runtime/types.h>

class File;
class BlockDevice;

class FilesystemDriver {
public:
    virtual ~FilesystemDriver() = default;
    virtual const char* name() const = 0;
    virtual File* mount(File* mount_point, BlockDevice* device) = 0;
};

#endif

#include <os.h>
#include <core/block_device.h>
#include <runtime/alloc.h>

extern "C" {
    void *memcpy(void *dest, const void *src, int n);
}

BlockDevice::BlockDevice(const char* name, u32 block_size)
    : Device(name), block_size_(block_size ? block_size : 512) {
}

BlockDevice::~BlockDevice() = default;

u32 BlockDevice::read(u32 pos, u8* buffer, u32 size) {
    if (!buffer || size == 0) {
        return ERROR_PARAM;
    }

    u32 lba = pos / block_size_;
    u32 offset = pos % block_size_;
    u32 total = size;
    u8 temp_block[1024];

    if (block_size_ > sizeof(temp_block)) {
        u8* temp = (u8*)kmalloc(block_size_);
        if (!temp) {
            return ERROR_MEMORY;
        }
        if (offset) {
            if (read_blocks(lba, 1, temp) != RETURN_OK) {
                kfree(temp);
                return RETURN_FAILURE;
            }
            u32 chunk = block_size_ - offset;
            if (chunk > total) {
                chunk = total;
            }
            memcpy(buffer, temp + offset, chunk);
            buffer += chunk;
            total -= chunk;
            lba++;
        }
        while (total >= block_size_) {
            if (read_blocks(lba, 1, buffer) != RETURN_OK) {
                kfree(temp);
                return RETURN_FAILURE;
            }
            buffer += block_size_;
            total -= block_size_;
            lba++;
        }
        if (total) {
            if (read_blocks(lba, 1, temp) != RETURN_OK) {
                kfree(temp);
                return RETURN_FAILURE;
            }
            memcpy(buffer, temp, total);
        }
        kfree(temp);
        return RETURN_OK;
    }

    if (offset) {
        if (read_blocks(lba, 1, temp_block) != RETURN_OK) {
            return RETURN_FAILURE;
        }
        u32 chunk = block_size_ - offset;
        if (chunk > total) {
            chunk = total;
        }
        memcpy(buffer, temp_block + offset, chunk);
        buffer += chunk;
        total -= chunk;
        lba++;
    }

    while (total >= block_size_) {
        if (read_blocks(lba, 1, buffer) != RETURN_OK) {
            return RETURN_FAILURE;
        }
        buffer += block_size_;
        total -= block_size_;
        lba++;
    }

    if (total) {
        if (read_blocks(lba, 1, temp_block) != RETURN_OK) {
            return RETURN_FAILURE;
        }
        memcpy(buffer, temp_block, total);
    }

    return RETURN_OK;
}

u32 BlockDevice::write(u32 pos, u8* buffer, u32 size) {
    if (!buffer || size == 0) {
        return ERROR_PARAM;
    }

    u32 lba = pos / block_size_;
    u32 offset = pos % block_size_;
    u32 total = size;
    u8 temp_block[1024];

    if (block_size_ > sizeof(temp_block)) {
        u8* temp = (u8*)kmalloc(block_size_);
        if (!temp) {
            return ERROR_MEMORY;
        }

        if (offset) {
            if (read_blocks(lba, 1, temp) != RETURN_OK) {
                kfree(temp);
                return RETURN_FAILURE;
            }
            u32 chunk = block_size_ - offset;
            if (chunk > total) {
                chunk = total;
            }
            memcpy(temp + offset, buffer, chunk);
            if (write_blocks(lba, 1, temp) != RETURN_OK) {
                kfree(temp);
                return RETURN_FAILURE;
            }
            buffer += chunk;
            total -= chunk;
            lba++;
        }

        while (total >= block_size_) {
            if (write_blocks(lba, 1, buffer) != RETURN_OK) {
                kfree(temp);
                return RETURN_FAILURE;
            }
            buffer += block_size_;
            total -= block_size_;
            lba++;
        }

        if (total) {
            if (read_blocks(lba, 1, temp) != RETURN_OK) {
                kfree(temp);
                return RETURN_FAILURE;
            }
            memcpy(temp, buffer, total);
            if (write_blocks(lba, 1, temp) != RETURN_OK) {
                kfree(temp);
                return RETURN_FAILURE;
            }
        }

        kfree(temp);
        return RETURN_OK;
    }

    if (offset) {
        if (read_blocks(lba, 1, temp_block) != RETURN_OK) {
            return RETURN_FAILURE;
        }
        u32 chunk = block_size_ - offset;
        if (chunk > total) {
            chunk = total;
        }
        memcpy(temp_block + offset, buffer, chunk);
        if (write_blocks(lba, 1, temp_block) != RETURN_OK) {
            return RETURN_FAILURE;
        }
        buffer += chunk;
        total -= chunk;
        lba++;
    }

    while (total >= block_size_) {
        if (write_blocks(lba, 1, buffer) != RETURN_OK) {
            return RETURN_FAILURE;
        }
        buffer += block_size_;
        total -= block_size_;
        lba++;
    }

    if (total) {
        if (read_blocks(lba, 1, temp_block) != RETURN_OK) {
            return RETURN_FAILURE;
        }
        memcpy(temp_block, buffer, total);
        if (write_blocks(lba, 1, temp_block) != RETURN_OK) {
            return RETURN_FAILURE;
        }
    }

    return RETURN_OK;
}

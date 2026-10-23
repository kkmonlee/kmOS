#ifndef __MY_BOOT__
#define __MY_BOOT__

#include <runtime/types.h>

#define MULTIBOOT_MAGIC 0x2BADB002

struct multiboot_info {
    u32 flags;
    u32 low_mem;
    u32 high_mem;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    struct { u32 num, size, addr, shndx; } elf_sec;
    u32 mmap_length;
    u32 mmap_addr;
    u32 drives_length;
    u32 drives_addr;
    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;
    u32 vbe_control_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_seg;
    u16 vbe_interface_off;
    u16 vbe_interface_len;
} __attribute__((packed));

struct multiboot_module {
    u32 start, end, string, reserved;
};

struct multiboot_mmap_entry {
    u32 size;
    u64 address;
    u64 length;
    u32 type;
} __attribute__((packed));

class MultibootMemoryMap {
public:
    MultibootMemoryMap(const void *data, u32 length)
        : cursor_((const u8 *)data), remaining_(length), valid_(data != nullptr || !length) {}

    bool next(multiboot_mmap_entry &entry) {
        if (!valid_ || !remaining_) return false;
        if (remaining_ < sizeof(u32)) return valid_ = false;
        const multiboot_mmap_entry *raw = (const multiboot_mmap_entry *)cursor_;
        if (raw->size < 20 || raw->size > remaining_ - sizeof(u32)) return valid_ = false;
        entry = *raw;
        u32 bytes = raw->size + sizeof(u32);
        cursor_ += bytes;
        remaining_ -= bytes;
        return true;
    }

    bool valid() const { return valid_; }

private:
    const u8 *cursor_;
    u32 remaining_;
    bool valid_;
};

static_assert(__builtin_offsetof(multiboot_info, mmap_length) == 44, "Multiboot layout");
static_assert(sizeof(multiboot_info) == 88, "Multiboot size");

/* VBE controller information.  */
struct vbe_controller
{
  unsigned char signature[4];
  unsigned short version;
  unsigned long oem_string;
  unsigned long capabilities;
  unsigned long video_mode;
  unsigned short total_memory;
  unsigned short oem_software_rev;
  unsigned long oem_vendor_name;
  unsigned long oem_product_name;
  unsigned long oem_product_rev;
  unsigned char reserved[222];
  unsigned char oem_data[256];
} __attribute__((packed));

/* VBE mode information.  */
struct vbe_mode
{
  unsigned short mode_attributes;
  unsigned char win_a_attributes;
  unsigned char win_b_attributes;
  unsigned short win_granularity;
  unsigned short win_size;
  unsigned short win_a_segment;
  unsigned short win_b_segment;
  unsigned long win_func;
  unsigned short bytes_per_scanline;

  /* >=1.2 */
  unsigned short x_resolution;
  unsigned short y_resolution;
  unsigned char x_char_size;
  unsigned char y_char_size;
  unsigned char number_of_planes;
  unsigned char bits_per_pixel;
  unsigned char number_of_banks;
  unsigned char memory_model;
  unsigned char bank_size;
  unsigned char number_of_image_pages;
  unsigned char reserved0;

  /* direct color */
  unsigned char red_mask_size;
  unsigned char red_field_position;
  unsigned char green_mask_size;
  unsigned char green_field_position;
  unsigned char blue_mask_size;
  unsigned char blue_field_position;
  unsigned char reserved_mask_size;
  unsigned char reserved_field_position;
  unsigned char direct_color_mode_info;

  /* >=2.0 */
  unsigned long phys_base;
  unsigned long reserved1;
  unsigned short reversed2;

  /* >=3.0 */
  unsigned short linear_bytes_per_scanline;
  unsigned char banked_number_of_image_pages;
  unsigned char linear_number_of_image_pages;
  unsigned char linear_red_mask_size;
  unsigned char linear_red_field_position;
  unsigned char linear_green_mask_size;
  unsigned char linear_green_field_position;
  unsigned char linear_blue_mask_size;
  unsigned char linear_blue_field_position;
  unsigned char linear_reserved_mask_size;
  unsigned char linear_reserved_field_position;
  unsigned long max_pixel_clock;

  unsigned char reserved3[189];
} __attribute__((packed));

#endif
#ifndef __API_IOCTL__
#define __API_IOCTL__

// Device operations
#define DEV_GET_TYPE 0x01   // Get device type
#define DEV_GET_STATE 0x02  // Get device state
#define DEV_GET_FORMAT 0x03 // Get device format

// Device types
#define DEV_TYPE_TTY 0x01
#define DEV_TYPE_DISK 0x02
#define DEV_TYPE_FB 0x03
#define DEV_TYPE_HID 0x04 // Human Interface Device

// Device formats
#define DEV_FORMAT_CHAR 0x01
#define DEV_FORMAT_BLOCK 0x02
#define DEV_FORMAT_FB 0x03

// Device states
#define DEV_STATE_OK 0x01
#define DEV_STATE_NOTREADY 0x02 // Device not ready

#endif

#ifndef __API_FB__
#define __API_FB__

struct fb_info
{
  unsigned int w;     // Width of the screen
  unsigned int h;     // Height of the screen
  char bpp;           // Bits per pixel (color depth)
  char state;         // State of the framebuffer
  unsigned int *vmem; // Pointer to video memory
};

enum
{
  FB_NOT_ACTIVE = 0,
  FB_ACTIVE = 1,
};

#define API_FB_IS_AVAILABLE 0x801 // Check if the framebuffer API is available
#define API_FB_GET_INFO 0x802     // Get current framebuffer information
#define API_FB_GET_BINFO 0x803    // Get best possible framebuffer information
#define API_FB_SET_INFO 0x804     // Set framebuffer information

#endif

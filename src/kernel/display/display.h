
#ifndef _DISPLAY_H
#define _DISPLAY_H

#include <multiboot.h>

// u8
typedef enum {
	DisplayCmd_NOP,
	DisplayCmd_DrawColor,   // x: u16, y: u16, w: u16, h: u16, color: u32
	DisplayCmd_DrawTex,     // x: u16, y: u16, w: u16, h: u16, pixel_buf: []u32
	DisplayCmd_SwapBuffers, // x: u16, y: u16, w: u16, h: u16
} DisplayCmd;

int display_init(multiboot_framebuffer_tag *fb_tag);

#endif


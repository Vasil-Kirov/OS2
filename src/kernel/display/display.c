

#include <drivers.h>
#include <kmalloc.h>
#include <kmem.h>
#include <multiboot.h>
#include <bstream.h>
#include "display.h"

typedef struct {
	Spinlock frontbuf_lock;
	u32 *fb;
	u32 pitch;
	u32 width;
	u32 height;
	Device dev;
} Display;

typedef struct {
	u32 *backbuffer;
	Display *d;
} ChrdevDisplay;

int display_open(struct INode *inode, struct File *file)
{
	ChrdevDisplay *dev = kzalloc(sizeof(ChrdevDisplay));
	if (!dev)
		return -ENOMEM;

	Display *d = container_of((Device *)inode->priv, Display, dev);
	dev->d = d;
	size_t bufsize = d->pitch * d->height;

	file->priv = dev;
	dev->backbuffer = kzalloc(bufsize);
	if (!dev->backbuffer) {
		kfree(dev);
		return -ENOMEM;
	}
	return 0;
}

void display_close(struct File *file)
{
	ChrdevDisplay *dev = file->priv;
	if (dev)
		kfree(dev->backbuffer);
	kfree(dev);
}

static bool display_validate_coords(Display *d, u16 x, u16 y, u16 w, u16 h)
{
	if (x > d->width || x + w > d->width)
		return false;
	if (y > d->height || y + h > d->height)
		return false;
	return true;
}

static inline u32 *framebuffer_row_start(u32 *fb, u16 y, u32 pitch)
{
	return (u32 *)((u8 *)fb + y * pitch);
}

ssize_t display_write(struct File *file, void *buf, size_t size)
{
	ChrdevDisplay *dev = file->priv;

	BinaryStream bs;
	bs_init(&bs, buf, size);
	if (bs_remaining_size(&bs) == 0)
		return -EINVAL;
	u8 cmd = bs_r8(&bs);
	switch (cmd)
	{
		case DisplayCmd_NOP:
		{
		} break;
		case DisplayCmd_DrawColor:
		{
			u16 x = bs_r16(&bs);
			u16 y = bs_r16(&bs);
			u16 w = bs_r16(&bs);
			u16 h = bs_r16(&bs);
			u32 color = bs_r32(&bs);
			if (bs.err != 0)
				return -EINVAL;

			if (!display_validate_coords(dev->d, x, y, w, h))
				return -EINVAL;

			for (u16 iy = y; iy < y+h; ++iy) {
				u32 *at = framebuffer_row_start(dev->backbuffer, iy, dev->d->pitch)+x;
				for (u16 ix = 0; ix < w; ++ix) {
					*at++ = color;
				}
			}
		} break;
		case DisplayCmd_DrawTex:
		{
		} break;
		case DisplayCmd_SwapBuffers:
		{
			u16 x = bs_r16(&bs);
			u16 y = bs_r16(&bs);
			u16 w = bs_r16(&bs);
			u16 h = bs_r16(&bs);
			if (bs.err != 0)
				return -EINVAL;

			if (!display_validate_coords(dev->d, x, y, w, h))
				return -EINVAL;

			for (u16 iy = y; iy < y+h; ++iy) {
				u32 *at = framebuffer_row_start(dev->backbuffer, (iy-y), dev->d->pitch);
				u32 *fb = framebuffer_row_start(dev->d->fb, iy, dev->d->pitch);
				for (u16 ix = x; ix < x+w; ++ix) {
					fb[ix] = *at++;
				}
			}
		} break;
		default:
			return -EINVAL;
	}
	return size - bs_remaining_size(&bs);
}

#if 0
ssize_t display_read(struct File *file, void *buf, size_t size)
{
}
#endif

static FileOps display_fops = {
	.open = display_open,
	.close = display_close,
	.write = display_write,
	.read = NULL,
};

int display_init(multiboot_framebuffer_tag *fb_tag)
{
	Display *d = kzalloc(sizeof(Display));
	if (!d)
		return -ENOMEM;

	size_t bufsize = fb_tag->framebuffer_pitch * fb_tag->framebuffer_height;
	d->fb = kmem_map_phy_addr(fb_tag->framebuffer_addr, bufsize, PAGE_FLAG_RW);
	if (!d->fb)
		return -ENOMEM;
	d->pitch = fb_tag->framebuffer_pitch;
	d->width = fb_tag->framebuffer_width;
	d->height = fb_tag->framebuffer_height;

	d->dev.name = STR_LIT("display");
	return chrdev_create(&d->dev, display_fops);
}


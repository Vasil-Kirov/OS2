#include <drivers.h>
#include <io.h>

#define PS2DATA_PORT 0x60
#define PS2CMD_PORT 0x64

enum PS2Commands {
	PS2CMD_DisableFirst = 0xAD,
	PS2CMD_DisableSecond = 0xA7,
	PS2CMD_ReadRAM = 0x20, // + offset
};

int ps2kb_driver_init()
{
	// @TODO: AML interpreter to discover if ps/2 controller is available
	out8(PS2CMD_PORT, PS2CMD_DisableFirst);
	out8(PS2CMD_PORT, PS2CMD_DisableSecond);
	in8(PS2DATA_PORT);
	out8(PS2CMD_PORT, PS2CMD_ReadRAM);
	u8 cfg = in8(PS2DATA_PORT);
	return 0;
}

void ps2kb_driver_exit()
{
}

BUILTIN_DRIVER("ps2kb_driver", ps2kb_driver_init, ps2kb_driver_exit)


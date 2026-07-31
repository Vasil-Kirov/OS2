
#ifndef _SERIAL_H
#define _SERIAL_H

#include <kcommon.h>

int init_serial();
void serial_write(char c);
u8 serial_read();



#endif


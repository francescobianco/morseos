/* Host stand-in for the Telink system tick: 32-bit, 16 MHz, wraps every 268 s like the real one. */
#ifndef CLOCK_H
#define CLOCK_H

#include "tl_common.h"

#define sys_tick_per_us 16

u32  clock_time(void);
int  clock_time_exceed(u32 ref, u32 us);
void sleep_us(u32 us);  /* blocks the main loop, as on the device */

#endif

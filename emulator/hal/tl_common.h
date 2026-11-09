/* Host stand-in for the Telink SDK's tl_common.h: only the types MorseOS uses. Keep it free
 * of libc headers such as unistd.h: the firmware defines its own access(), send(), ... */
#ifndef TL_COMMON_H
#define TL_COMMON_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;

#endif

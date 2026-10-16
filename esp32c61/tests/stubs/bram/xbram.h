#pragma once
#include <stdint.h>
typedef uint32_t u32;
void XBram_WriteReg(u32 base,u32 offset,u32 value);
u32 XBram_ReadReg(u32 base,u32 offset);

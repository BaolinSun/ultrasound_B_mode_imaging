#ifndef TEMP_TEST_IIC_H
#define TEMP_TEST_IIC_H
#include "xiicps_hw.h"
typedef struct {u32 BaseAddress;} XIicPs_Config;
typedef struct {XIicPs_Config Config;u32 IsReady;} XIicPs;
#endif

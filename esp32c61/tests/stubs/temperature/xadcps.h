#ifndef TEMP_TEST_ADC_H
#define TEMP_TEST_ADC_H
#include "xadcps_hw.h"
typedef struct {u32 BaseAddress;} XAdcPs_Config;
typedef struct {XAdcPs_Config Config;} XAdcPs;
XAdcPs_Config *XAdcPs_LookupConfig(UINTPTR);
int XAdcPs_CfgInitialize(XAdcPs*,XAdcPs_Config*,u32);
#endif

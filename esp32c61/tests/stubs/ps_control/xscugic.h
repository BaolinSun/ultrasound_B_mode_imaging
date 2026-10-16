#include "xuartps.h"
typedef struct {int unused;} XScuGic;
typedef void (*Xil_ExceptionHandler)(void*);
#define XSCUGIC_SPI_CPU1_MASK 1U
int XScuGic_Connect(XScuGic*,u32,Xil_ExceptionHandler,void*);
void XScuGic_Enable(XScuGic*,u32);
void XScuGic_InterruptMaptoCpu(XScuGic*,u8,u32);

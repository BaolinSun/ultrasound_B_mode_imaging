#ifndef __timer_h
#define __timer_h

#include "xparameters.h"
#include "xscugic.h"
#include "../main.h"
#include "xttcps.h"
typedef struct {
	u32 OutputHz;	/* Output frequency */
	XInterval Interval;	/* Interval value */
	u8 Prescaler;	/* Prescaler value */
	u16 Options;	/* Option settings */
} TmrCntrSetup;

void timer_init(void);
void timer_set_delay(u32 us);
void timer_delay(void);

void delay_handler(XTtcPs *CallBackRef,u32 StatusEvent);

#endif

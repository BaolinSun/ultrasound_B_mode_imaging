#include "timer.h"
#include "xttcps.h"
#include "xinterrupt_wrap.h"

XTtcPs delay_timer;
#define TTC_INT_IRQ_ID \
	((u32)(XGet_IntrId(XPAR_XTTCPS_0_INTERRUPTS) + \
		XGet_IntrOffset(XPAR_XTTCPS_0_INTERRUPTS)))

typedef struct {
	s32 delay_time;
	s32 intr_times;
} delay_ctrl_struct;

delay_ctrl_struct delay_ctrl;

void timer_init(void) {
	XTtcPs_Config *cfg;
	TmrCntrSetup TimerSetup;
	cfg = XTtcPs_LookupConfig(XPAR_XTTCPS_0_BASEADDR);
	XTtcPs_CfgInitialize(&delay_timer, cfg, cfg->BaseAddress);

	TimerSetup.Options = XTTCPS_OPTION_INTERVAL_MODE
			| XTTCPS_OPTION_WAVE_DISABLE;
	TimerSetup.OutputHz = 1000000;
	TimerSetup.Interval = 0;
	TimerSetup.Prescaler = 0;
	XTtcPs_SetOptions(&delay_timer, TimerSetup.Options);
	XTtcPs_CalcIntervalFromFreq(&delay_timer, TimerSetup.OutputHz,
			&TimerSetup.Interval, &TimerSetup.Prescaler);

	/*
	 * Set the interval and prescale
	 */
	XTtcPs_SetInterval(&delay_timer, TimerSetup.Interval);
	XTtcPs_SetPrescaler(&delay_timer, TimerSetup.Prescaler);

	XScuGic_Connect(&xInterruptController,
			TTC_INT_IRQ_ID,
			(Xil_ExceptionHandler) delay_handler,
			(void *) &delay_timer);

	XTtcPs_SetStatusHandler(&delay_timer, &delay_timer,
			(XTtcPs_StatusHandler) delay_handler);
	/*
	 * Enable the interrupt for the Timer counter
	 */
	XScuGic_Enable(&xInterruptController,
			TTC_INT_IRQ_ID);

	/*
	 * Enable the interrupts for the tick timer/counter
	 * We only care about the interval timeout.
	 */
	XTtcPs_EnableInterrupts(&delay_timer, XTTCPS_IXR_INTERVAL_MASK);
}

void timer_set_delay(u32 us) {
//	delay_ctrl.delay_time = us;
	delay_ctrl.intr_times = us;
}

void timer_delay(void) {
	XTtcPs_Start(&delay_timer);
	while (delay_ctrl.intr_times > 0)
		;
	XTtcPs_Stop(&delay_timer);
}

void delay_handler(XTtcPs *CallBackRef, u32 StatusEvent) {
	static u32 XTtcPsStatusReg;
	XTtcPsStatusReg = XTtcPs_GetInterruptStatus(CallBackRef);
	XTtcPs_ClearInterruptStatus(CallBackRef, XTtcPsStatusReg);
	delay_ctrl.intr_times--;
}

#ifndef USB_IRQ_TEST_H
#define USB_IRQ_TEST_H
extern unsigned mock_cpsr;
#define XIL_EXCEPTION_IRQ 128U
#define mfcpsr() mock_cpsr
#define mtcpsr(x) (mock_cpsr=(x))
#define Xil_ExceptionDisableMask(x) (mock_cpsr|=(x))
#endif

#ifndef USB_HW_TEST_H
#define USB_HW_TEST_H
#include "xil_types.h"
typedef struct { u32 token; const u8 *user; } XUsbPs_dTD;
typedef struct { u32 words[2]; } XUsbPs_dQH;
typedef struct { XUsbPs_dTD *dTDHead,*dTDTail,*dTDs; XUsbPs_dQH *dQH; } XUsbPs_EpIn;
typedef struct {
    struct { u32 BaseAddress; } Config;
    struct {
        struct { XUsbPs_EpIn In; } Ep[3];
        struct { struct { unsigned NumBufs; } In; } EpCfg[3];
    } DeviceConfig;
} XUsbPs;
#define XPAR_XUSBPS_0_BASEADDR 0U
#define XUSBPS_CMD_OFFSET 0U
#define XUSBPS_EPRDY_OFFSET 4U
#define XUSBPS_EPPRIME_OFFSET 8U
#define XUSBPS_EPFLUSH_OFFSET 12U
#define XUSBPS_PORTSCR1_OFFSET 16U
#define XUSBPS_EPCOMPL_OFFSET 20U
#define XUSBPS_CMD_RS_MASK 1U
#define XUSBPS_CMD_RST_MASK 2U
#define XUSBPS_PORTSCR_CCS_MASK 1U
#define XUSBPS_PORTSCR_PSPD_MASK 0x0c000000U
#define XUSBPS_EP_DIRECTION_IN 1U
#define XUSBPS_dTDTOKEN 0U
#define XUSBPS_dQHdTDNLP 0U
#define XUSBPS_dQHdTDTOKEN 1U
#define XUSBPS_dTDTOKEN_XERR_MASK 8U
#define XUSBPS_dTDTOKEN_BUFERR_MASK 32U
#define XUSBPS_dTDTOKEN_HALT_MASK 64U
#define XUSBPS_dTDTOKEN_ACTIVE_MASK 128U
#define XUSBPS_dTDTOKEN_LEN_MASK 0x7fff0000U
#define XST_SUCCESS 0
#define XUsbPs_WritedTD(p,o,v) ((p)->token=(v))
#define XUsbPs_ReaddTD(p,o) ((p)->token)
#define XUsbPs_WritedQH(p,o,v) ((p)->words[o]=(v))
#define XUsbPs_dTDFlushCache(p) ((void)(p))
#define XUsbPs_dTDInvalidateCache(p) ((void)(p))
#define XUsbPs_dQHFlushCache(p) ((void)(p))
u32 XUsbPs_ReadReg(u32,u32);
void XUsbPs_WriteReg(u32,u32,u32);
void XUsbPs_EpDisable(XUsbPs*,unsigned,unsigned);
void XUsbPs_EpEnable(XUsbPs*,unsigned,unsigned);
int XUsbPs_EpBufferSend(XUsbPs*,unsigned,const u8*,u32);
void Xil_DCacheInvalidateRange(UINTPTR,u32);
unsigned Beamform_GetDownsample(void);
void usb_reset_command_queue_isr(void);
int xil_printf(const char*,...);
#endif

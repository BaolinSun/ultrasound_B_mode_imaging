#include "pus_oper.h"
#include "../AFE5832/afe5832.h"



u32 pus_chip_init(void)
{
	AFE5832_init();
	DEBUG_PRINT("AFE OK\r\n");
//	Pl_Reset();
	sleep(1);
	DEBUG_PRINT("TX7332 OK\r\n");
}
u32 pus_init(void)
{
	pus_chip_init();
}

#ifndef UB_CPU1_PLATFORM_H
#define UB_CPU1_PLATFORM_H
#include "cpu1_control.h"
#include "xil_io.h"
#include "xiltimer.h"
static unsigned cpu1_platform_core(void *ctx)
{
    uint32_t mpidr;
    (void)ctx;
    __asm__ volatile("mrc p15, 0, %0, c0, c0, 5" : "=r"(mpidr));
    return mpidr & 0xFFU;
}
static uint32_t cpu1_platform_read(void *ctx, uint32_t addr)
{ (void)ctx; return Xil_In32(addr); }
static void cpu1_platform_write(void *ctx, uint32_t addr, uint32_t value)
{ (void)ctx; Xil_Out32(addr, value); }
static void cpu1_platform_barrier(void *ctx)
{ (void)ctx; __asm__ volatile("dsb sy\n\tisb" ::: "memory"); }
static uint64_t cpu1_platform_ticks(void *ctx)
{ XTime t; (void)ctx; XTime_GetTime(&t); return t; }
static cpu1_result cpu1_platform_disable(void)
{
    const cpu1_ops ops = {0, cpu1_platform_core, cpu1_platform_read, cpu1_platform_write,
        cpu1_platform_barrier, cpu1_platform_ticks, (uint64_t)COUNTS_PER_SECOND / 100U};
    return cpu1_disable(&ops);
}
#endif

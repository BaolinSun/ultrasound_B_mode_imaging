#ifndef UB_CPU0_IDLE_CORE_H
#define UB_CPU0_IDLE_CORE_H
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    void *ctx;
    bool (*quiet)(void *); /* cheap software power/transport state */
    uint32_t (*lock)(void *); /* saves CPSR and masks IRQ */
    void (*restore)(void *,uint32_t);
    bool (*pending)(void *); /* IRQ-masked, read-only; includes hardware proof */
    uint32_t (*next_ms)(void *); /* zero means work is due NOW */
    bool (*arm)(void *,uint32_t);
    void (*wait)(void *); /* DSB / WFI / ISB, IRQ remains masked */
    bool (*finish)(void *,uint32_t,uint64_t); /* disarm our timer, no other's IRQ */
    uint64_t (*us)(void *);
} cpu0_idle_ops;
typedef struct {
    cpu0_idle_ops io;
    bool enabled;
    uint32_t entries;
    uint64_t wait_us;
    const char *error;
} cpu0_idle_core;
void cpu0_idle_core_init(cpu0_idle_core *,const cpu0_idle_ops *,bool enabled);
void cpu0_idle_core_try(cpu0_idle_core *);
/* ceil(freq * ms / (256*1000)), saturating for the private 32-bit counter. */
uint32_t cpu0_idle_timer_count(uint32_t frequency,uint32_t ms);
#endif

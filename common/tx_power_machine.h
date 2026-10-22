#ifndef UB_TX_POWER_MACHINE_H
#define UB_TX_POWER_MACHINE_H
#include <stdint.h>
#include <stdbool.h>
#include "tx_power_regs.h"
typedef enum { TX_UNKNOWN,TX_READY,TX_QUIESCING,TX_SLEEPING,TX_WAKING,TX_FAULT } tx_power_state;
typedef struct {
    void *ctx;
    uint64_t (*ms)(void *);
    uint32_t (*read)(void *,unsigned);
    void (*write)(void *,unsigned,uint32_t);
    int (*stop)(void *); /* RUN=0, HV=0; do not interrupt current TX config */
    bool (*idle)(void *); /* transport DMA/leases retired */
    int (*pause)(void *); /* DDR drain, or validated retained drain certificate */
    void (*gate)(void *,bool);
} tx_power_ops;
typedef struct {
    tx_power_ops io;
    tx_power_state state;
    uint32_t owner,sequence;
    uint64_t deadline,settle;
    unsigned phase;
    const char *error;
} tx_power_machine;
void tp_init(tx_power_machine *,const tx_power_ops *);
int tp_prepare(tx_power_machine *,uint32_t);
int tp_sleep(tx_power_machine *,uint32_t);
int tp_wake(tx_power_machine *);
void tp_poll(tx_power_machine *);
bool tp_allowed(const tx_power_machine *);
const char *tp_name(tx_power_state);
#endif

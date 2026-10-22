#ifndef UB_AFE_POWER_MACHINE_H
#define UB_AFE_POWER_MACHINE_H
#include <stdint.h>
#include <stdbool.h>
#include "afe_power_regs.h"
typedef enum { AFE_UNKNOWN, AFE_READY, AFE_QUIESCING, AFE_SLEEP, AFE_WAKING, AFE_FAULT } afe_state;
typedef struct {
    void *ctx;
    uint64_t (*ms)(void *);
    uint32_t (*read)(void *, unsigned);
    void (*write)(void *, unsigned, uint32_t);
    int (*pdn)(void *, bool);
    int (*stop)(void *);
    bool (*idle)(void *);
    int (*pause)(void *);
    int (*save)(void *);
    int (*restore)(void *);
    void (*gate)(void *, bool);
} afe_power_ops;
typedef struct {
    afe_power_ops io;
    afe_state state;
    uint32_t owner, epoch;
    uint64_t deadline, settle;
    unsigned phase;
    const char *error;
} afe_power_machine;
void ap_init(afe_power_machine *, const afe_power_ops *);
int ap_prepare(afe_power_machine *, uint32_t owner);
int ap_sleep(afe_power_machine *, uint32_t owner);
int ap_wake(afe_power_machine *);
void ap_poll(afe_power_machine *);
const char *ap_name(afe_state);
#endif

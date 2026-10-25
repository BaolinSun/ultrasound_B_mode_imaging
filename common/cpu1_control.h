#ifndef UB_CPU1_CONTROL_H
#define UB_CPU1_CONTROL_H
#include <stdint.h>

#define CPU1_SLCR_LOCK 0xF8000004U
#define CPU1_SLCR_UNLOCK 0xF8000008U
#define CPU1_SLCR_LOCKSTA 0xF800000CU
#define CPU1_RESET_CTRL 0xF8000244U
#define CPU1_RESET_BIT 0x00000002U
#define CPU1_CLOCK_BIT 0x00000020U
#define CPU1_DISABLED_MASK (CPU1_RESET_BIT | CPU1_CLOCK_BIT)

typedef enum {
    CPU1_OK, CPU1_NOT_CPU0, CPU1_UNLOCK_FAILED, CPU1_RESET_TIMEOUT,
    CPU1_CLOCK_TIMEOUT, CPU1_OTHER_BITS_CHANGED, CPU1_LOCK_RESTORE_FAILED
} cpu1_error;
typedef struct {
    void *ctx;
    unsigned (*core)(void *);
    uint32_t (*read)(void *, uint32_t);
    void (*write)(void *, uint32_t, uint32_t);
    void (*barrier)(void *);
    uint64_t (*ticks)(void *);
    uint64_t timeout_ticks; /* 10 ms per stage with a running timer */
} cpu1_ops;
typedef struct {
    uint32_t before, after;
    cpu1_error error;
    unsigned lock_restored;
} cpu1_result;
cpu1_result cpu1_disable(const cpu1_ops *ops);
const char *cpu1_error_name(cpu1_error error);
#endif

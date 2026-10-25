#include "cpu1_control.h"

/* A stopped/broken timer must not turn a power optimization into a boot hang. */
#define CPU1_POLL_BUDGET 100000U
static cpu1_error wait_bits(const cpu1_ops *o, cpu1_result *r, uint32_t bits,
                           cpu1_error timeout)
{
    uint64_t start = o->ticks(o->ctx);
    for (unsigned n = 0; n < CPU1_POLL_BUDGET; ++n) {
        o->barrier(o->ctx);
        r->after = o->read(o->ctx, CPU1_RESET_CTRL);
        if ((r->after & ~CPU1_DISABLED_MASK) != (r->before & ~CPU1_DISABLED_MASK))
            return CPU1_OTHER_BITS_CHANGED;
        if ((r->after & bits) == bits) return CPU1_OK;
        if (o->ticks(o->ctx) - start >= o->timeout_ticks) return timeout;
    }
    return timeout;
}
cpu1_result cpu1_disable(const cpu1_ops *o)
{
    cpu1_result r = {0, 0, CPU1_NOT_CPU0, 1};
    if (o->core(o->ctx) != 0) return r; /* No MMIO from another core. */
    r.before = r.after = o->read(o->ctx, CPU1_RESET_CTRL);
    r.error = CPU1_OK;
    if ((r.before & CPU1_DISABLED_MASK) == CPU1_DISABLED_MASK) return r;
    uint32_t locked = o->read(o->ctx, CPU1_SLCR_LOCKSTA) & 1U;
    if (locked) {
        o->write(o->ctx, CPU1_SLCR_UNLOCK, 0xDF0DU);
        o->barrier(o->ctx);
        if (o->read(o->ctx, CPU1_SLCR_LOCKSTA) & 1U) r.error = CPU1_UNLOCK_FAILED;
    }
    if (r.error == CPU1_OK) {
        /* Preserve CPU0/PERI_RST and never clear either CPU1 control bit. */
        o->write(o->ctx, CPU1_RESET_CTRL, r.before | CPU1_RESET_BIT);
        r.error = wait_bits(o, &r, CPU1_RESET_BIT, CPU1_RESET_TIMEOUT);
        if (r.error == CPU1_OK) {
            o->write(o->ctx, CPU1_RESET_CTRL, r.after | CPU1_DISABLED_MASK);
            r.error = wait_bits(o, &r, CPU1_DISABLED_MASK, CPU1_CLOCK_TIMEOUT);
        }
    }
    if (locked) {
        o->write(o->ctx, CPU1_SLCR_LOCK, 0x767BU);
        o->barrier(o->ctx);
    }
    r.lock_restored = (o->read(o->ctx, CPU1_SLCR_LOCKSTA) & 1U) == locked;
    if (!r.lock_restored && r.error == CPU1_OK) r.error = CPU1_LOCK_RESTORE_FAILED;
    return r;
}
const char *cpu1_error_name(cpu1_error e)
{
    static const char *const names[] = {"NONE", "NOT_CPU0", "UNLOCK_FAILED", "RESET_TIMEOUT",
        "CLOCK_TIMEOUT", "OTHER_BITS_CHANGED", "LOCK_RESTORE_FAILED"};
    return (unsigned)e < sizeof(names)/sizeof(names[0]) ? names[e] : "UNKNOWN";
}

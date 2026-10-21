#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Admission headroom, NOT a global heap reservation: other tasks allocate.
 * Two copies budget lwIP + Wi-Fi; timeout still bounds resource waits. */
static inline bool tcp_memory_headroom(size_t available,size_t bytes,size_t reserve)
{
    return available>=reserve && bytes<=(available-reserve)/2U;
}
/* Preserve the same reserve and two-copy budget, but allow a smaller write.
 * Prefer whole MSS units; do not create a stream of tiny TCP allocations.
 * A genuine final short tail is permitted only if its full budget fits. */
static inline size_t tcp_memory_write_limit(size_t available,size_t requested,
                                          size_t reserve,size_t quantum)
{
    if(!requested || !quantum || available<=reserve) return 0;
    size_t budget=(available-reserve)/2U;
    if(budget>=requested) return requested;
    if(requested<quantum || budget<quantum) return 0;
    return (budget/quantum)*quantum;
}

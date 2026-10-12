#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Admission headroom, NOT a global heap reservation: other tasks allocate.
 * Two copies budget lwIP + Wi-Fi; timeout still bounds resource waits. */
static inline bool tcp_memory_headroom(size_t available,size_t bytes,size_t reserve)
{
    return available>=reserve && bytes<=(available-reserve)/2U;
}

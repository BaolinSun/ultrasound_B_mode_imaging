#pragma once
#include "raw_transfer.h"
#include "../protocol/bridge_protocol.h"
raw_result_t psram_framed_send(const uint8_t *data, size_t length,
    const bridge_frame_t *frame, uint8_t *staging, uint64_t deadline,
    const raw_io_t *io, size_t *sent);

/* Suppress the SDK's inline physical MMIO when using its real register
 * definitions on the host. Production builds never include this file. */
#include "xil_types.h"
#include "xil_io.h"
#include "xil_assert.h"
#include "xparameters.h"
#define XIL_TYPES_H
#define XIL_IO_H
#define XIL_ASSERT_H
#define XPARAMETERS_H

#include "sdio_cmd_engine.h"
enum { BLK_COUNT=0x06, ARG=0x08, TRANSFER_COMMAND=0x0c, PRESENT=0x24,
       TIMEOUT_CONTROL=0x2e, NORMAL=0x30, ERROR=0x32 };
int sdio_cmd_execute(const sdio_cmd_io_t *io,uint32_t cmd,uint32_t arg,
                     uint16_t blocks,uint16_t mode,unsigned allow_data,unsigned timeout_us)
{
    uint64_t start=io->now_us(io->ctx);
    /* A stopped timer must not turn this into an infinite loop. */
    unsigned polls=100000U;
    uint32_t inhibit=1U|((cmd&0x20U)?2U:0U);
    while(io->read32(io->ctx,PRESENT)&inhibit) {
        if(!polls-- || io->now_us(io->ctx)-start>=timeout_us) return SDIO_CMD_TIMEOUT;
    }
    io->write16(io->ctx,NORMAL,0xffffU);
    io->write16(io->ctx,ERROR,0xf3ffU);
    io->write16(io->ctx,BLK_COUNT,blocks);
    io->write8(io->ctx,TIMEOUT_CONTROL,0x0eU);
    io->write32(io->ctx,ARG,arg);
    io->write32(io->ctx,TRANSFER_COMMAND,((cmd&0x3fffU)<<16)|mode);
    for(;;) {
        uint16_t normal=io->read16(io->ctx,NORMAL);
        if((normal&0x8000U) || io->read16(io->ctx,ERROR)) return SDIO_CMD_ERROR;
        if(normal&1U) {
            io->write16(io->ctx,NORMAL,1U);
            return SDIO_CMD_OK;
        }
        /* CMD53 may expose a data event before software observes CC.
         * Leave data events pending for the payload/completion handler. */
        if(allow_data && (normal&0x12U)) return SDIO_CMD_OK;
        /* Idle by itself is NOT proof that a command completed. */
        if(!polls-- || io->now_us(io->ctx)-start>=timeout_us) return SDIO_CMD_TIMEOUT;
    }
}

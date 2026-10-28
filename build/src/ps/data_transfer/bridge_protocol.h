#ifndef UB_BRIDGE_PROTOCOL_H
#define UB_BRIDGE_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BR_FRAME_BYTES 787456U
#define BR_BLOCK_BYTES 512U
#define BR_WINDOW_BLOCKS 16U
#define BR_UDP_PAYLOAD 1440U
#define BR_UDP_HEADER 32U
#define BR_MAGIC UINT32_C(0x55425344)
#define BR_VERSION 1U
#define BR_SDIO_VERSION 4U
#define BR_HEALTH_OK 0x87U /* valid | Wi-Fi | sender | target */
#define BR_STOP_TIMEOUT_US 5000000LL
static inline bool br_image_bytes_valid(uint32_t bytes) {
    for(unsigned d=0;d<=4;d++) if(bytes==((6144U>>d)+8U)*128U) return true;
    return false;
}
/* Logical shared registers, not physical CMD52 addresses. Host-owned fields
 * are committed by changing CMD_SEQ last, and immutable until CMD_ACK. */
enum { BR_CMD=0, BR_CMD_SEQ=1, BR_DEVICE=2, BR_STATUS=3, BR_CREDIT=4,
       BR_GRANT=5, BR_WINDOW_ACK=6, BR_PROTOCOL=7, BR_CMD_ACK=8,
       BR_RESULT=9, BR_HOST_MODE=10, BR_STREAM_KIND=11,
       BR_SOURCE_FLAGS=12, BR_SOURCE_DOWN=13, BR_SOURCE_SLOT=14, BR_SOURCE_OCCUPANCY=15,
       BR_SOURCE_DROPS=16, BR_SOURCE_ERRORS=20, BR_SOURCE_LATEST=24, BR_SOURCE_PUBLISHED=52, BR_SESSION=32, BR_FRAME_ID=36,
       BR_LENGTH=40, BR_CRC=44, BR_SESSION_ECHO=48,
       BR_STOP_REQ=56, BR_STOP_ACK=57, BR_HEALTH=58, BR_HEARTBEAT=59 };
/* STOP_REQ is slave-owned, nonzero token committed before STOPPING status.
 * Host echoes STOP_ACK only after CMD53/DMA is idle and its DDR lease retired.
 * Session plus token identifies this stop; ordinary stop never resets SDIO. */
enum { BR_NOP=0, BR_BEGIN=1, BR_ABORT=2 };
enum { BR_READY=1, BR_STREAM=0x10, BR_BACKPRESSURE=0x20, BR_STOPPING=0x40 };
enum { BR_ACCEPTED=0, BR_DONE=1, BR_REJECTED=2, BR_BAD_CRC=3, BR_FAULT=4, BR_CANCELLED=5 };
typedef struct { uint32_t session_id, frame_id, frame_bytes, crc32; } bridge_frame_t;

static inline void br_put_be32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static inline void br_put_be16(uint8_t *p, unsigned v) { p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
/* IEEE reflected CRC32, incremental argument/result use zlib convention. */
static inline uint32_t br_crc32(uint32_t crc, const uint8_t *p, size_t n) {
    static const uint32_t t[16]={0,0x1db71064,0x3b6e20c8,0x26d930ac,
        0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,
        0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    crc=~crc;
    while(n--) { crc^=*p++; crc=(crc>>4)^t[crc&15]; crc=(crc>>4)^t[crc&15]; }
    return ~crc;
}
static inline uint8_t br_next_seq(uint8_t n) { return n==255 ? 1 : (uint8_t)(n+1); }
static inline unsigned br_window(size_t remaining, unsigned loaded) {
    size_t blocks=(remaining+BR_BLOCK_BYTES-1)/BR_BLOCK_BYTES;
    if(loaded>BR_WINDOW_BLOCKS) loaded=BR_WINDOW_BLOCKS;
    return blocks<loaded ? (unsigned)blocks : loaded;
}
static inline bool br_meta_valid(const bridge_frame_t *m, uint32_t session, size_t maximum) {
    return m && m->session_id==session && session && m->frame_id &&
        m->frame_bytes && m->frame_bytes<=maximum;
}
static inline void br_udp_header(uint8_t *p, const bridge_frame_t *m, uint32_t offset, unsigned bytes) {
    br_put_be32(p,BR_MAGIC); p[4]=BR_VERSION; p[5]=BR_UDP_HEADER; br_put_be16(p+6,0);
    br_put_be32(p+8,m->session_id); br_put_be32(p+12,m->frame_id);
    br_put_be32(p+16,m->frame_bytes); br_put_be32(p+20,offset);
    br_put_be16(p+24,bytes); br_put_be16(p+26,offset/BR_UDP_PAYLOAD); br_put_be32(p+28,m->crc32);
}
#endif

#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint16_t tcpwnd_size_t;
#define TCP_MSS 1440U
#define TCP_SND_BUF 5760U
#define TCP_SND_QUEUELEN 80U
enum tcp_state { CLOSED, ESTABLISHED };
struct tcp_pcb {
    enum tcp_state state;
    void *unsent, *unacked;
    unsigned snd_queuelen;
    tcpwnd_size_t snd_buf, ssthresh;
};

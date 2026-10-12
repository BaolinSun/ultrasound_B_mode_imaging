#pragma once
#include "tcp.h"
#define NETCONN_TCP 0x10
#define NETCONN_UDP 0x20
#define NETCONNTYPE_GROUP(type) ((type) & 0xf0)
#define NETCONN_NONE 0
struct netconn { int type, state; union { struct tcp_pcb *tcp; } pcb; };

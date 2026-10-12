#pragma once
#include "lwip/api.h"
struct lwip_sock { struct netconn *conn; };
struct lwip_sock *lwip_socket_dbg_get_socket(int fd);

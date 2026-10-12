#pragma once
typedef int err_t;
#define ERR_OK 0
struct tcpip_api_call_data { err_t err; };
typedef err_t (*tcpip_api_call_fn)(struct tcpip_api_call_data *);
err_t tcpip_api_call(tcpip_api_call_fn fn, struct tcpip_api_call_data *call);

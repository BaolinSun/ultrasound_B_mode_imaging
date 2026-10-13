#include "tcp_socket_tuning.h"
#include "lwip/priv/sockets_priv.h"
#include "lwip/priv/tcpip_priv.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
static struct tcp_pcb upload, http;
static struct netconn conn;
static struct lwip_sock sock;
static bool in_core, fail_call;
static unsigned calls;
struct lwip_sock *lwip_socket_dbg_get_socket(int fd) {
    assert(in_core); /* All PCB access MUST run in the serialized core. */
    return fd==3 ? &sock : NULL;
}
err_t tcpip_api_call(tcpip_api_call_fn fn, struct tcpip_api_call_data *call) {
    calls++;
    if(fail_call)return -1;
    in_core=true;err_t result=fn(call);in_core=false;return result;
}
static void reset(void) {
    upload=(struct tcp_pcb){.state=CLOSED,.snd_buf=TCP_SND_BUF,.ssthresh=TCP_SND_BUF};
    http=upload;conn=(struct netconn){.type=NETCONN_TCP,.state=NETCONN_NONE};
    conn.pcb.tcp=&upload;sock.conn=&conn;fail_call=false;
}
int main(void) {
    unsigned actual=99;
    reset();assert(tcp_upload_set_send_buffer(-1,28800,&actual)==EINVAL && actual==0);
    assert(tcp_upload_set_send_buffer(3,28800,NULL)==EINVAL);
    assert(tcp_upload_set_send_buffer(3,2879,&actual)==EINVAL);
    assert(tcp_upload_set_send_buffer(3,65536,&actual)==EINVAL);
    assert(calls==0);
    assert(tcp_upload_set_send_buffer(4,28800,&actual)==EBADF);
    sock.conn=NULL;assert(tcp_upload_set_send_buffer(3,28800,&actual)==EBADF);
    reset();conn.type=NETCONN_UDP;assert(tcp_upload_set_send_buffer(3,28800,&actual)==EPROTOTYPE);
    reset();conn.pcb.tcp=NULL;assert(tcp_upload_set_send_buffer(3,28800,&actual)==EPROTOTYPE);
    reset();conn.state=1;assert(tcp_upload_set_send_buffer(3,28800,&actual)==EBUSY);
    reset();upload.state=ESTABLISHED;assert(tcp_upload_set_send_buffer(3,28800,&actual)==EBUSY);
    reset();upload.unsent=&conn;assert(tcp_upload_set_send_buffer(3,28800,&actual)==EBUSY);
    reset();upload.unacked=&conn;assert(tcp_upload_set_send_buffer(3,28800,&actual)==EBUSY);
    reset();upload.snd_queuelen=1;assert(tcp_upload_set_send_buffer(3,28800,&actual)==EBUSY);
    reset();upload.snd_buf--;assert(tcp_upload_set_send_buffer(3,28800,&actual)==EBUSY);
    /* Every queue grade must reject a capacity that exceeds its own ceiling. */
    reset();assert(tcp_upload_set_send_buffer(3,(TCP_SND_QUEUELEN/4U+1U)*TCP_MSS,&actual)==EINVAL);
    assert(upload.snd_buf==TCP_SND_BUF && actual==0);
    reset();fail_call=true;assert(tcp_upload_set_send_buffer(3,28800,&actual)==EIO);
    assert(upload.snd_buf==TCP_SND_BUF && actual==0);
    const unsigned sizes[]={5760,11520,23040,28800,46080,57600};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        reset();
        if (TCP_SND_QUEUELEN < (4U*sizes[i]+TCP_MSS-1U)/TCP_MSS) {
            assert(tcp_upload_set_send_buffer(3,sizes[i],&actual)==EINVAL);
            assert(actual==0 && upload.snd_buf==TCP_SND_BUF && http.snd_buf==TCP_SND_BUF);
            continue;
        }
        assert(tcp_upload_set_send_buffer(3,sizes[i],&actual)==0);
        assert(actual==sizes[i] && upload.snd_buf==sizes[i]);
        assert(upload.ssthresh==sizes[i]);
        assert(http.snd_buf==5760 && http.ssthresh==5760); /* No other PCB mutation. */
        upload.state=ESTABLISHED;
        assert(tcp_upload_set_send_buffer(3,sizes[i],&actual)==EBUSY && actual==0);
    }
    puts("PASS: upload-only capacity/core serialization/invalid socket/active PCB/queue limit/core failure/HTTP isolation");
}

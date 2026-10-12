#include "tcp_socket_tuning.h"
#include <errno.h>
#include <stddef.h>
#include "esp_idf_version.h"
#include "lwip/tcp.h"
#include "lwip/api.h"
#include "lwip/priv/sockets_priv.h"
#include "lwip/priv/tcpip_priv.h"

/* IDF 6.1's Kconfig documents TCP_SNDBUF but this SDK does not implement it.
 * Isolate this version-specific adapter here, never patch the installed SDK.
 * The worker owns the descriptor for the entire synchronous core call: no
 * concurrent connect/send/close. PCB access runs exclusively in tcpip_thread
 * (or under the lwIP core lock when enabled). Upgrade requires review. */
#if ESP_IDF_VERSION_MAJOR != 6 || ESP_IDF_VERSION_MINOR != 1
#error "Review tcp_socket_tuning against the new IDF/lwIP before upgrading"
#endif

typedef struct {
    struct tcpip_api_call_data call; /* Must be first. */
    int fd, error;
    unsigned bytes, actual;
} tuning_call_t;

static err_t tune_in_core(struct tcpip_api_call_data *base)
{
    tuning_call_t *request = (tuning_call_t *)base;
    struct lwip_sock *sock = lwip_socket_dbg_get_socket(request->fd);
    request->error = EBADF;
    if (!sock || !sock->conn) return ERR_OK;
    request->error = EPROTOTYPE;
    if (NETCONNTYPE_GROUP(sock->conn->type) != NETCONN_TCP || !sock->conn->pcb.tcp)
        return ERR_OK;
    struct tcp_pcb *pcb = sock->conn->pcb.tcp;
    request->error = EBUSY;
    if (sock->conn->state != NETCONN_NONE || pcb->state != CLOSED || pcb->unsent ||
        pcb->unacked || pcb->snd_queuelen || pcb->snd_buf != TCP_SND_BUF)
        return ERR_OK;
    /* TCP_SND_QUEUELEN is set consistently for lwIP and this component in
     * CMake; the normal small default would otherwise bottleneck this PCB. */
    request->error = EINVAL;
    if (TCP_SND_QUEUELEN < (4U * request->bytes + TCP_MSS - 1U) / TCP_MSS)
        return ERR_OK;
    pcb->snd_buf = (tcpwnd_size_t)request->bytes;
    /* Mirror tcp_new()'s initial threshold for this capacity. Do not force
     * cwnd or change congestion control/retransmission on a live connection. */
    pcb->ssthresh = (tcpwnd_size_t)request->bytes;
    request->actual = (unsigned)pcb->snd_buf;
    request->error = request->actual == request->bytes ? 0 : EOVERFLOW;
    return ERR_OK;
}

int tcp_upload_set_send_buffer(int fd, unsigned bytes, unsigned *actual)
{
    if (actual) *actual = 0;
    if (fd < 0 || !actual || bytes < 2U * TCP_MSS || bytes > 65535U)
        return EINVAL;
    tuning_call_t request = {.fd = fd, .error = EIO, .bytes = bytes};
    if (tcpip_api_call(tune_in_core, &request.call) != ERR_OK) return EIO;
    if (!request.error) *actual = request.actual;
    return request.error;
}

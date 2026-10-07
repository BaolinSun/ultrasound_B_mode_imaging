#include "raw_transfer.h"
#include "config_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t now;
    raw_result_t abort;
    unsigned cancel_after, sends, transient, permanent_after, delays, packets, retries;
    uint8_t collected[50000];
    size_t collected_bytes, last_length;
    unsigned callbacks;
} mock_t;
static uint64_t clock_ms(void *p) { return ((mock_t *)p)->now; }
static raw_result_t abort_check(void *p)
{
    mock_t *m = p;
    return m->cancel_after && m->sends >= m->cancel_after ? RAW_CANCELLED : m->abort;
}
static int send_mock(void *p, const uint8_t *data, size_t n)
{
    mock_t *m = p;
    assert(n > 0 && n <= RAW_UDP_BYTES);
    ++m->sends;
    if (m->permanent_after && m->sends >= m->permanent_after) return -1;
    if (m->transient) { --m->transient; return 1; }
    assert(m->collected_bytes + n <= sizeof(m->collected));
    memcpy(m->collected + m->collected_bytes, data, n);
    m->collected_bytes += n;
    m->last_length = n;
    return 0;
}
static void delay_mock(void *p, unsigned ms) { mock_t *m = p; m->now += ms; ++m->delays; }
static void progress_mock(void *p, size_t n, unsigned packets, unsigned retries)
{
    mock_t *m = p;
    assert(n == m->collected_bytes);
    m->packets = packets; m->retries = retries;
}
static void done_mock(raw_result_t r, size_t n, void *p)
{
    (void)r; (void)n; ++((mock_t *)p)->callbacks;
}
static raw_result_t transfer(mock_t *m, const uint8_t *data, size_t n, uint64_t deadline, size_t *sent)
{
    uint8_t stage[RAW_UDP_BYTES];
    raw_io_t io = {m, clock_ms, abort_check, send_mock, delay_mock, progress_mock};
    return psram_raw_send(data, n, stage, deadline, &io, sent);
}
static void reserve_tests(void)
{
    raw_session_t s = {0}; mock_t m = {0}; uint8_t bytes[8];
    assert(raw_reserve(&s, NULL, 8, 8, true, true, true, done_mock, &m) == RAW_INVALID);
    assert(raw_reserve(&s, bytes, 0, 8, true, true, true, done_mock, &m) == RAW_INVALID);
    assert(raw_reserve(&s, bytes, 9, 8, true, true, true, done_mock, &m) == RAW_INVALID);
    assert(raw_reserve(&s, bytes, 8, 8, false, true, true, done_mock, &m) == RAW_INVALID);
    assert(raw_reserve(&s, bytes, 8, 8, true, false, true, done_mock, &m) == RAW_NOT_READY);
    assert(raw_reserve(&s, bytes, 8, 8, true, true, false, done_mock, &m) == RAW_NOT_READY);
    assert(raw_reserve(&s, bytes, 8, 8, true, true, true, done_mock, &m) == RAW_OK);
    assert(raw_reserve(&s, bytes, 8, 8, true, true, true, done_mock, &m) == RAW_BUSY);
    raw_completion_t callback = NULL; void *ctx = NULL;
    assert(raw_finish(&s, &callback, &ctx)); callback(RAW_CANCELLED, 0, ctx);
    assert(!raw_finish(&s, &callback, &ctx)); assert(m.callbacks == 1);
    assert(!s.busy && !s.data);
    assert(raw_reserve(&s, bytes, 8, 8, true, true, true, NULL, NULL) == RAW_INVALID);
    for (raw_result_t terminal = RAW_OK; terminal <= RAW_SEND_ERROR; ++terminal) {
        assert(raw_reserve(&s, bytes, 8, 8, true, true, true, done_mock, &m) == RAW_OK);
        assert(raw_finish(&s, &callback, &ctx));
        callback(terminal, 0, ctx);
        assert(!raw_finish(&s, &callback, &ctx));
    }
    assert(m.callbacks == 9);
}
static void span_tests(void)
{
    assert(raw_span_inside(100, 8, 100, 8));
    assert(raw_span_inside(103, 5, 100, 8));
    assert(!raw_span_inside(103, 6, 100, 8));
    assert(!raw_span_inside(99, 1, 100, 8));
    assert(!raw_span_inside(108, 1, 100, 8));
    assert(!raw_span_inside(100, 0, 100, 8));
    assert(!raw_span_inside(100, 1, 100, 0));
    assert(!raw_span_inside(UINTPTR_MAX - 1, 8, UINTPTR_MAX - 1, 8));
    assert(!raw_span_inside(100, SIZE_MAX, 100, 8));
}
static void send_tests(void)
{
    uint8_t data[50000]; raw_pattern_fill(data, sizeof(data));
    assert(data[0] == 0 && data[1] == 0 && data[2] == 0xc3 && data[3] == 0xa5 && data[4] == 1);
    const size_t sizes[] = {1, 4, 1439, 1440, 1441, 2880, 46081, 50000};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        mock_t m = {0}; size_t sent;
        assert(transfer(&m, data, sizes[i], 30000, &sent) == RAW_OK);
        assert(sent == sizes[i] && m.collected_bytes == sent && !memcmp(data, m.collected, sent));
        assert(m.packets == (sizes[i] + 1439) / 1440);
        assert(m.last_length == (sizes[i] - 1) % 1440 + 1);
        assert(m.delays == (m.packets - 1) / 32);
    }
    mock_t retry = {.transient = 2}; size_t sent;
    assert(transfer(&retry, data, 1441, 30000, &sent) == RAW_OK);
    assert(retry.retries == 2 && retry.delays == 2 && !memcmp(data, retry.collected, sent));
    mock_t exhausted = {.transient = 101};
    assert(transfer(&exhausted, data, 8, 30000, &sent) == RAW_SEND_ERROR);
    assert(exhausted.sends == 101 && sent == 0 && exhausted.retries == 100);
    mock_t last_attempt = {.transient = 100};
    assert(transfer(&last_attempt, data, 8, 30000, &sent) == RAW_OK);
    assert(last_attempt.sends == 101 && sent == 8);
    mock_t permanent = {.permanent_after = 2};
    assert(transfer(&permanent, data, 3000, 30000, &sent) == RAW_SEND_ERROR && sent == 1440);
    mock_t cancel = {.cancel_after = 1};
    assert(transfer(&cancel, data, 3000, 30000, &sent) == RAW_CANCELLED && sent == 1440);
    mock_t disconnected = {.abort = RAW_LINK_DOWN};
    assert(transfer(&disconnected, data, 8, 30000, &sent) == RAW_LINK_DOWN && sent == 0);
    mock_t timeout = {.transient = 100};
    assert(transfer(&timeout, data, 8, 2, &sent) == RAW_TIMEOUT && timeout.sends == 2);
    mock_t expired = {.now = 30000};
    assert(transfer(&expired, data, 8, 30000, &sent) == RAW_TIMEOUT && expired.sends == 0);
    for (raw_result_t r = RAW_OK; r <= RAW_SEND_ERROR; ++r) assert(strcmp(raw_result_name(r), "unknown"));
    assert(!strcmp(raw_result_name((raw_result_t)99), "unknown"));
}
static app_config_t stored;
static int storage_fail;
static int persist_mock(void *ctx, const app_config_t *c)
{
    (void)ctx; if (storage_fail) return 42; stored = *c; return 0;
}
static void config_tests(void)
{
    app_config_t a = {.version = 1, .target_port = 50001, .ap_password = "abcdefgh12345678"};
    assert(config_valid(&a));
    const char *valid[] = {"", "192.168.1.1", "10.1.0.2", "223.255.255.255"};
    const char *invalid[] = {"256.0.1.1", "1.2.3", "1.2.3.4.5", " 1.2.3.4", "1.2.3.4 ",
        "-1.2.3.4", "0.0.0.0", "127.0.0.1", "224.1.1.1", "255.255.255.255", "1..3.4", "1.0000.3.4", "001.2.3.4", "1.2.3.a"};
    for (unsigned i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) assert(config_ipv4_valid(valid[i], true));
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) assert(!config_ipv4_valid(invalid[i], true));
    assert(!config_ipv4_valid(NULL, true) && !config_ipv4_valid("", false));
    app_config_t b = a; strcpy(b.ssid, "new_network"); strcpy(b.target_ip, "192.168.1.100");
    storage_fail = 1;
    assert(config_commit(&a, &b, persist_mock, NULL) == 42 && a.ssid[0] == 0);
    storage_fail = 0;
    assert(config_commit(&a, &b, persist_mock, NULL) == 0 && !memcmp(&a, &b, sizeof(a)));
    app_config_t restored = stored; assert(config_valid(&restored) && !memcmp(&restored, &b, sizeof(b)));
    b.target_port = 0; assert(config_commit(&a, &b, persist_mock, NULL) == -1);
    b = a; b.version = 2; assert(!config_valid(&b));
    assert(!config_valid(NULL));
    b = a; memset(b.ssid, 'a', sizeof(b.ssid)); assert(!config_valid(&b));
    b = a; memset(b.password, 'a', sizeof(b.password)); assert(!config_valid(&b));
    b = a; memset(b.target_ip, 'a', sizeof(b.target_ip)); assert(!config_valid(&b));
    b = a; memset(b.ap_password, 'a', sizeof(b.ap_password)); assert(!config_valid(&b));
    b = a; strcpy(b.password, "short"); assert(!config_valid(&b));
    b = a; memset(b.password, 'a', 64); b.password[64] = 0; assert(config_valid(&b));
    b.password[0] = 'z'; assert(!config_valid(&b));
    b.password[0] = '/'; assert(!config_valid(&b));
    b.password[0] = ':'; assert(!config_valid(&b));
    for (unsigned i = 0; i < 64; ++i) b.password[i] = "0123456789abcdefABCDEF"[i % 22];
    assert(config_valid(&b));
    b = a; strcpy(b.ap_password, "short"); assert(!config_valid(&b));
    b = a; strcpy(b.target_ip, "bad"); assert(!config_valid(&b));
}
int main(void)
{
    span_tests(); reserve_tests(); send_tests(); config_tests();
    puts("PASS: C core span/admission/completion/raw bytes/retry/abort/deadline/config transaction tests");
    return 0;
}

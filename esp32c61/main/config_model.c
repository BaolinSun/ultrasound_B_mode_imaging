#include "config_model.h"
#include <string.h>

bool config_ipv4_valid(const char *s, bool allow_empty)
{
    if (!s) return false;
    if (!*s) return allow_empty;
    unsigned octets[4];
    for (unsigned i = 0; i < 4; ++i) {
        const char *first = s;
        unsigned value = 0, digits = 0;
        while (*s >= '0' && *s <= '9') {
            value = value * 10U + (unsigned)(*s++ - '0');
            if (++digits > 3 || value > 255U) return false;
        }
        if (!digits || (digits > 1 && *first == '0') ||
            (i < 3 ? *s++ != '.' : *s != '\0')) return false;
        octets[i] = value;
    }
    /* A configurable target must be unicast, not unspecified/loopback/multicast. */
    return octets[0] > 0 && octets[0] < 224 && octets[0] != 127;
}

bool config_valid(const app_config_t *c)
{
    if (!c || c->version != APP_CONFIG_VERSION || !c->target_port ||
        !memchr(c->ssid, 0, sizeof(c->ssid)) ||
        !memchr(c->password, 0, sizeof(c->password)) ||
        !memchr(c->target_ip, 0, sizeof(c->target_ip))) return false;
    size_t n = strlen(c->password);
    if (n && n < 8) return false;
    if (n == 64) {
        for (size_t i = 0; i < n; ++i) {
            char v = c->password[i];
            if (!((v >= '0' && v <= '9') || (v >= 'a' && v <= 'f') ||
                  (v >= 'A' && v <= 'F'))) return false;
        }
    }
    return config_ipv4_valid(c->target_ip, true);
}

int config_commit(app_config_t *active, const app_config_t *candidate,
                  config_persist_t persist, void *ctx)
{
    if (!config_valid(candidate)) return -1;
    int result = persist(ctx, candidate);
    if (result == 0) *active = *candidate;
    return result;
}

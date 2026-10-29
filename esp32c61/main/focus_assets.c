#include "esp_http_server.h"
#include "focus_assets.h"
#include <string.h>

extern const unsigned char focus_pack_start[] asm("_binary_focus_profiles_pack_start");
extern const unsigned char focus_pack_end[] asm("_binary_focus_profiles_pack_end");

/* No decompression, table-sized heap allocation or cJSON use on the ESP32. */
esp_err_t focus_asset_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    if (!strcmp(req->uri, "/focus/catalog.json")) {
        return httpd_resp_send(req, focus_catalog_json, sizeof(focus_catalog_json) - 1);
    }
    for (unsigned i = 0; i < sizeof(focus_assets) / sizeof(focus_assets[0]); ++i) {
        const focus_asset_t *asset = &focus_assets[i];
        if (strcmp(req->uri, asset->url)) continue;
        size_t size = (size_t)(focus_pack_end - focus_pack_start);
        if (asset->offset > size || asset->length > size - asset->offset) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Focus asset range error");
        }
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, (const char *)focus_pack_start + asset->offset, asset->length);
    }
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Unknown focus profile");
}

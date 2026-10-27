#include "config_store.h"
#include <string.h>
#include "device_identity.h"
#include <stdio.h>
#ifdef PROVISIONING_HOST_TEST
#include "provisioning_fake.h"
#else
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#endif

static SemaphoreHandle_t lock;
static app_config_t current;
static device_identity_t identity;
static const char *identity_state="unprepared";
static bool legacy_pending;
/* Original v1 blob ABI: migrate only after its credential copy is durable. */
typedef struct {
    uint32_t version;char ssid[33],password[65],target_ip[16];uint16_t target_port;char ap_password[17];
} config_v1_t;
_Static_assert(sizeof(config_v1_t)==140,"legacy settings ABI");
bool device_store_get(device_identity_t *out) {
    if(strcmp(identity_state,"ready"))return false;
    if(out)*out=identity;
    return true;
}
const char *device_store_state(void) {return identity_state;}

static int persist(void *ctx, const app_config_t *candidate)
{
    (void)ctx;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("udp_config", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "settings", candidate, sizeof(*candidate));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static bool legacy_convert(const config_v1_t *v,app_config_t *out) {
    if(v->version!=1 || !memchr(v->ap_password,0,sizeof v->ap_password) || strlen(v->ap_password)!=16)return false;
    app_config_t c={.version=APP_CONFIG_VERSION,.target_port=v->target_port};
    memcpy(c.ssid,v->ssid,sizeof c.ssid);memcpy(c.password,v->password,sizeof c.password);memcpy(c.target_ip,v->target_ip,sizeof c.target_ip);
    if(!config_valid(&c))return false;
    *out=c;return true;
}
static void load_identity(const config_v1_t *legacy) {
    uint8_t mac[6];char text[DEVICE_IDENTITY_BYTES];size_t n=sizeof text;nvs_handle_t h;
    identity_state="invalid";
    if(esp_efuse_mac_get_default(mac)!=ESP_OK)return;
    esp_err_t e=nvs_flash_init_partition("device_nvs");
    if(e!=ESP_OK){identity_state=e==ESP_ERR_NOT_FOUND?"missing_partition":"invalid";return;}
    e=nvs_open_from_partition("device_nvs","device",NVS_READONLY,&h);
    if(e==ESP_OK){e=nvs_get_str(h,"identity",text,&n);nvs_close(h);}
    if(e==ESP_OK) {
        if(!n || n>sizeof text || text[n-1] || strlen(text)+1!=n || !device_identity_parse(text,&identity))return;
        if(memcmp(mac,identity.mac,6)){identity_state="mac_mismatch";return;}
        identity_state="ready";return;
    }
    if(e!=ESP_ERR_NVS_NOT_FOUND)return; /* Never replace corrupt/different credentials. */
    identity_state="unprepared";
    if(!legacy)return;
    device_identity_t candidate={0};uint8_t ap_mac[6];
    if(esp_read_mac(ap_mac,ESP_MAC_WIFI_SOFTAP)!=ESP_OK){identity_state="migration_failed";return;}
    memcpy(candidate.mac,mac,6);memcpy(candidate.password,legacy->ap_password,17);
    snprintf(candidate.ssid,sizeof candidate.ssid,"Ultrasound-C61-%02X%02X%02X",ap_mac[3],ap_mac[4],ap_mac[5]);
    if(!device_identity_format(&candidate,text,sizeof text)){identity_state="migration_failed";return;}
    identity_state="migration_failed";
    e=nvs_open_from_partition("device_nvs","device",NVS_READWRITE,&h);
    if(e!=ESP_OK)return;
    e=nvs_set_str(h,"identity",text);if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);
    if(e!=ESP_OK)return;
    char verify[DEVICE_IDENTITY_BYTES];n=sizeof verify;
    e=nvs_open_from_partition("device_nvs","device",NVS_READONLY,&h);
    if(e!=ESP_OK)return;
    e=nvs_get_str(h,"identity",verify,&n);nvs_close(h);
    if(e!=ESP_OK || n!=strlen(text)+1 || memcmp(text,verify,n))return;
    identity=candidate;identity_state="ready";
}
esp_err_t config_store_init(void) {
    esp_err_t e=nvs_flash_init();if(e!=ESP_OK)return e; /* No automatic erasure. */
    lock=xSemaphoreCreateMutex();if(!lock)return ESP_ERR_NO_MEM;
    current=(app_config_t){.version=APP_CONFIG_VERSION,.target_port=50001};
    union {config_v1_t v1;app_config_t v2;} stored={0};
    size_t n=sizeof stored;nvs_handle_t h;bool valid=false,legacy=false;
    e=nvs_open("udp_config",NVS_READONLY,&h);
    if(e==ESP_OK){e=nvs_get_blob(h,"settings",&stored,&n);nvs_close(h);}
    if(e==ESP_OK) {
        if(n==sizeof(app_config_t) && config_valid(&stored.v2)){current=stored.v2;valid=true;}
        else if(n==sizeof(config_v1_t) && legacy_convert(&stored.v1,&current)){valid=true;legacy=true;}
    } else if(e!=ESP_ERR_NVS_NOT_FOUND && e!=ESP_ERR_NVS_INVALID_LENGTH)return e;
    load_identity(legacy?&stored.v1:NULL);
    legacy_pending=legacy && !device_store_get(NULL);
    if(!device_store_get(NULL))ESP_LOGW("config","Device provisioning state=%s; no unknown/open hotspot",identity_state);
    if(!valid || (legacy && device_store_get(NULL))) {
        e=persist(NULL,&current);
        if(e!=ESP_OK && legacy) {
            ESP_LOGW("config","Network migration deferred; durable identity and legacy network retained");
            return ESP_OK;
        }
        return e;
    }
    return ESP_OK;
}

void config_store_get(app_config_t *config)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    *config = current;
    xSemaphoreGive(lock);
}

esp_err_t config_store_save(const app_config_t *config)
{
    if (!config_valid(config)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(lock, portMAX_DELAY);
    esp_err_t err = legacy_pending ? ESP_ERR_INVALID_STATE :
                    config_commit(&current, config, persist, NULL);
    xSemaphoreGive(lock);
    return err;
}

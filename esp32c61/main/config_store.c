#include "config_store.h"
#include <string.h>
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"

static SemaphoreHandle_t lock;
static app_config_t current;

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

esp_err_t config_store_init(void)
{
    /* Never erase unrelated NVS data automatically on an initialization error. */
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return err;
    lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    current = (app_config_t){.version = APP_CONFIG_VERSION, .target_port = 50001};
    nvs_handle_t handle;
    err = nvs_open("udp_config", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        app_config_t stored = {0};
        size_t size = sizeof(stored);
        err = nvs_get_blob(handle, "settings", &stored, &size);
        nvs_close(handle);
        if (err == ESP_OK && size == sizeof(stored) && config_valid(&stored)) {
            current = stored;
            return ESP_OK;
        }
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND &&
            err != ESP_ERR_NVS_INVALID_LENGTH) return err;
        ESP_LOGW("config", "Missing/invalid config; restoring provisioning defaults");
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    static const char alphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    uint8_t random_bytes[16];
    esp_fill_random(random_bytes, sizeof(random_bytes));
    for (size_t i = 0; i < sizeof(random_bytes); ++i)
        current.ap_password[i] = alphabet[random_bytes[i] % (sizeof(alphabet) - 1)];
    return persist(NULL, &current);
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
    esp_err_t err = config_commit(&current, config, persist, NULL);
    xSemaphoreGive(lock);
    return err;
}

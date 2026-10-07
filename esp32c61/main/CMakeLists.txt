idf_component_register(SRCS "app_main.c" "config_store.c" "wifi_manager.c"
                            "psram_udp.c" "web_server.c" "selftest.c"
                            "raw_transfer.c" "config_model.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server
                                lwip esp_timer esp_hw_support esp_psram heap
                       EMBED_FILES "index.html")

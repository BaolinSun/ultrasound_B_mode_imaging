idf_component_register(SRCS "zynq_uart_bridge.c" "uart_log_framer.c" "app_main.c" "config_store.c" "wifi_manager.c"
                            "psram_udp.c" "web_server.c" "selftest.c"
                            "raw_transfer.c" "config_model.c" "framed_transfer.c" "sdio_bridge.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server
                                lwip esp_timer esp_hw_support esp_psram heap esp_driver_sdio esp_driver_gpio
                                esp_driver_uart esp_driver_usb_serial_jtag
                       EMBED_FILES "index.html")

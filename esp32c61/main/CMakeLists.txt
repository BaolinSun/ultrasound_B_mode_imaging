idf_component_register(SRCS "zynq_uart_bridge.c" "uart_log_framer.c" "app_main.c" "config_store.c" "wifi_manager.c"
                            "psram_udp.c" "web_server.c" "selftest.c"
                            "raw_transfer.c" "config_model.c" "framed_transfer.c" "sdio_bridge.c" "bridge_crc.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server
                                lwip esp_timer esp_hw_support esp_psram heap esp_driver_sdio esp_driver_gpio
                                esp_driver_uart esp_driver_usb_serial_jtag esp_rom
                       EMBED_FILES "index.html")
# Optimize only the data-path files. Keep assertions and other application
# configuration unchanged; compiler -O2 takes precedence over global -Os.
set_source_files_properties(sdio_bridge.c psram_udp.c raw_transfer.c framed_transfer.c bridge_crc.c
                            PROPERTIES COMPILE_OPTIONS "-O2")

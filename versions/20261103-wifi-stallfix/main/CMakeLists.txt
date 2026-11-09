idf_component_register(SRCS "basic_uvc_stream.c" "wifi_preview.c"
                    REQUIRES fatfs esp_psram esp_driver_gpio esp_driver_uart
                             esp_driver_i2c esp_driver_i2s esp_codec_dev apriltag esp_timer
                             esp_driver_jpeg esp_wifi esp_netif esp_event nvs_flash esp_http_server
                    INCLUDE_DIRS "."
                    EMBED_FILES "voice_red.pcm" "voice_green.pcm" "voice_blue.pcm"
                    EMBED_TXTFILES "web/index.html")

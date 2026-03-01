idf_component_register(SRCS "ePaper.c" "img_hacking.c" "wifi_server.c" "arrivals.c"
                       INCLUDE_DIRS "."
                       REQUIRES epaper spiffs heap driver esp_http_server nvs_flash esp_http_client json)

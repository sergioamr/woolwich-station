idf_component_register(SRCS
    "esp_spiffs.c"
    "spiffs_cache.c"
    "spiffs_check.c"
    "spiffs_gc.c"
    "spiffs_hydrogen.c"
    "spiffs_nucleus.c"
    "spiffs_vfs.c"
    "mutex.c"
    "list.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "."
    REQUIRES spi_flash
)

idf_component_register(SRCS
    "EPD.c"
    "EPDspi.c"
    "EPD_2in9b.c"
    "DefaultFont.c"
    "DejaVuSans18.c"
    "DejaVuSans24.c"
    "SmallFont.c"
    "Ubuntu16.c"
    "comic24.c"
    "dejavuX.c"
    "minya24.c"
    "tooney32.c"
    INCLUDE_DIRS "."
    REQUIRES spidriver heap driver
)

idf_component_register(
    SRCS
        "main.cpp"

    INCLUDE_DIRS
        "."

    REQUIRES
        M5Unified
        fatfs
        sdmmc
        esp_driver_sdspi
        espressif__esp-serial-flasher
)

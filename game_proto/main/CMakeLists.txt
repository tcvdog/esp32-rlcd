idf_component_register(
    SRCS "main.cpp" "i2c_bsp.cpp" "codec_port.cpp" "dr_flac_impl.c" "ui_theme.cpp" "ui_audio.cpp"
    REQUIRES
        esp_timer
        driver
        esp_adc
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        app_update
        codec_board
        esp_codec_dev
        esp_http_client
        json
        fatfs
        esp_driver_sdmmc
        u8g2_st7305
        spi_flash
    INCLUDE_DIRS "./")

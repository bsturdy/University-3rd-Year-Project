idf_component_register(
    SRCS "src/WifiClass.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos log esp_wifi esp_event esp_timer nvs_flash lwip
)

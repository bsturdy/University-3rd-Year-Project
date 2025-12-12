idf_component_register(
    SRCS "src/UtilitiesClass.cpp"
    INCLUDE_DIRS "include"
    REQUIRES 
        esp_system
        esp_timer
        heap
        driver
        freertos
)

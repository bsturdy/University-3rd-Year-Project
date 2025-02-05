idf_component_register(
    SRCS "GpioConfig.cpp" "main.cpp" "WifiClass.cpp" "TimerClass.cpp"
    INCLUDE_DIRS "."
    #REQUIRES led_strip
)

add_compile_options(-H)
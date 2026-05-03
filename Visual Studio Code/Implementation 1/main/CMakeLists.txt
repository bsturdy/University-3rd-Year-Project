idf_component_register(
    SRCS 
        "main.cpp" 
        "tests.cpp"
        "throughput_tests.cpp"
    INCLUDE_DIRS "."
    REQUIRES 
        GpioClassLib 
        TimerClassLib
        WifiClassLib
        UtilitiesClassLib
)

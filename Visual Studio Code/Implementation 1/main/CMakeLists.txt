idf_component_register(
    SRCS 
        "main.cpp" 
        "packet_processors.cpp"
    INCLUDE_DIRS "."
    REQUIRES 
        GpioClassLib 
        TimerClassLib
        WifiClassLib
        UtilitiesClassLib
)

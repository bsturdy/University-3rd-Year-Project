#ifndef UtilitiesClass_H
#define UtilitiesClass_H

// Author - Ben Sturdy
// This file implements a class 'Utilities Class'. This class should be instantiated
// only once in a project.

#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "driver/temperature_sensor.h"

class UtilitiesClass
{
    private:    
        void InitTemperatureSensor(); 

        bool temp_sensor_ready;
        temperature_sensor_handle_t temp_handle;


    public:
        UtilitiesClass();
        ~UtilitiesClass();

        // Time since boot, in microseconds and milliseconds
        static uint64_t GetUptimeUs();
        static uint64_t GetUptimeMs();

        // Free heap, bytes
        static std::size_t GetFreeHeapBytes();

        // Last reset reason (raw enum from ESP-IDF)
        static int GetResetReasonRaw();

        // Approximate chip temperature in degrees Celsius.
        static float GetChipTemperatureC();
};


#endif
#ifndef UtilitiesClass_H
#define UtilitiesClass_H

// Author - Ben Sturdy
// This file implements a class 'Utilities Class'. This class should be instantiated
// only once in a project.

#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "driver/temperature_sensor.h"
#include "esp_wifi.h"

class UtilitiesClass
{
    private:    
        UtilitiesClass();
        ~UtilitiesClass();

        void InitTemperatureSensor(); 

        bool temp_sensor_ready;
        temperature_sensor_handle_t temp_handle;


    public:
        static UtilitiesClass& GetInstance();
        UtilitiesClass(const UtilitiesClass&) = delete;
        void operator=(const UtilitiesClass&) = delete;

        static uint64_t GetUptimeUs() { return static_cast<uint64_t>(esp_timer_get_time()); }
        static uint64_t GetUptimeMs() { return GetUptimeUs() / 1000ULL; }
        static size_t GetFreeHeapBytes() { return static_cast<size_t>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT)); }
        static int GetResetReasonRaw() { return static_cast<int>(esp_reset_reason()); }

        float GetChipTemperatureC();
        float GetWifiSignalStrength();
};


#endif
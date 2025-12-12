#include "UtilitiesClass.h"

// Author - Ben Sturdy
// This file implements a class 'Utilities Class'. This class should be instantiated
// only once in a project. 





//==============================================================================// 
//                                                                              //
//                            Utilities Class                                   //
//                                                                              //
//==============================================================================// 

#define TAG                     "Utilities Class"

static UtilitiesClass* ClassInstance;





//==============================================================================//
//                                                                              //
//            Constructors, Destructors, Internal Functions                     //
//                                                                              //
//==============================================================================// 

UtilitiesClass::UtilitiesClass()
    : temp_sensor_ready(false),
      temp_handle(nullptr)
{
    // Pointer to itself
    ClassInstance = this;
    InitTemperatureSensor();
}

UtilitiesClass::~UtilitiesClass()
{
    if (temp_sensor_ready && temp_handle != nullptr) {
        temperature_sensor_disable(temp_handle);
        temperature_sensor_uninstall(temp_handle);
    }
}

void UtilitiesClass::InitTemperatureSensor()
{
    temperature_sensor_config_t temp_cfg = {
        .range_min = -10,
        .range_max = 80,
    };

    // Install driver
    if (temperature_sensor_install(&temp_cfg, &temp_handle) != ESP_OK) {
        temp_sensor_ready = false;
        temp_handle = nullptr;
        return;
    }

    // Enable sensor
    if (temperature_sensor_enable(temp_handle) != ESP_OK) {
        temperature_sensor_uninstall(temp_handle);
        temp_handle = nullptr;
        temp_sensor_ready = false;
        return;
    }

    temp_sensor_ready = true;
}

uint64_t UtilitiesClass::GetUptimeUs()
{
    // Microseconds since boot
    return static_cast<uint64_t>(esp_timer_get_time());
}

uint64_t UtilitiesClass::GetUptimeMs()
{
    return GetUptimeUs() / 1000ULL;
}

std::size_t UtilitiesClass::GetFreeHeapBytes()
{
    return static_cast<std::size_t>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
}

int UtilitiesClass::GetResetReasonRaw()
{
    return static_cast<int>(esp_reset_reason());
}

float UtilitiesClass::GetChipTemperatureC()
{
    if (!ClassInstance->temp_sensor_ready || ClassInstance->temp_handle == nullptr) {
        return -999.0f;
    }

    float temp_c = 0.0f;
    if (temperature_sensor_get_celsius(ClassInstance->temp_handle, &temp_c) != ESP_OK) {
        return -999.0f;
    }

    return temp_c;
}
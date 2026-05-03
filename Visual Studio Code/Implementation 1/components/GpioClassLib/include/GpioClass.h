#ifndef GpioClass_H
#define GpioClass_H

// Author - Ben Sturdy
// This file implements a class 'GPIO Class'. This class should be instantiated
// only once in a project. This class provides general functions for the GPIO features
// that are found on the ESP32 S3 board - including control of the on board LED,
// controlling analog input and output pins, creating PWM functionality and more.

#include "esp_log.h"
#include "driver/rmt_tx.h"         
#include <cstdint>
#include "driver/gpio.h"

class GpioClass
{
    private:
        GpioClass();
        ~GpioClass();

        bool IsRuntimeLoggingEnabled = true;
        bool SetupRmtTxChannel(gpio_num_t GpioNumber, uint32_t Resolution);         // Configures a TX RMT Channel

        esp_err_t Error;
        rmt_tx_channel_config_t RmtConfig;
        bool RmtChannelSetupComplete = false;
        bool OnboardLedSetupComplete = false;



    public:
        // Singleton Instance
        static GpioClass& GetInstance();
        GpioClass(const GpioClass&) = delete;
        void operator=(const GpioClass&) = delete;

        static constexpr gpio_num_t CyclicProbePin   = GPIO_NUM_4;
        static constexpr gpio_num_t WatchdogProbePin = GPIO_NUM_5;
        bool ScopePinsInitialised = false;

        void SetupScopePins();

        static inline void SetProbeHigh(gpio_num_t Pin)
        {
            gpio_set_level(Pin, 1);
        }

        static inline void SetProbeLow(gpio_num_t Pin)
        {
            gpio_set_level(Pin, 0);
        }

        bool SetupOnboardLed();                                                     // Configures an RMT channel to communicate with the LED pin
        bool ChangeOnboardLedColour(uint8_t Red, uint8_t Green, uint8_t Blue);      // Changes the colour of the onboard LED

};

#endif

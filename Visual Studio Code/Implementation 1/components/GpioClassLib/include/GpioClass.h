#ifndef GpioClass_H
#define GpioClass_H

// Author - Ben Sturdy
// This file implements a class 'GPIO Class'. This class should be instantiated
// only once in a project. This class provides general functions for the GPIO features
// that are found on the ESP32 S3 board - including control of the on board LED,
// controlling analog input and output pins, creating PWM functionality and more.
// This class is a work in progress

#include "esp_log.h"
#include "driver/rmt_tx.h"         

class GpioClass
{
    private:
        bool IsRuntimeLoggingEnabled = true;

        bool SetupRmtTxChannel(gpio_num_t GpioNumber, uint32_t Resolution);         // Configures a TX RMT Channel


    protected:



    public:
        GpioClass();
        ~GpioClass();

        bool SetupOnboardLed();                                                     // Configures an RMT channel to communicate with the LED pin
        bool ChangeOnboardLedColour(uint8_t Red, uint8_t Green, uint8_t Blue);      // Changes the colour of the onboard LED

};



/*
static uint8_t LedState = 0;
static led_strip_handle_t LedStrip;

bool SetupOutputPin(uint8_t Pin, gpio_pullup_t PullUp, gpio_pulldown_t PullDown);
bool SetupNeopixel(uint8_t Pin, uint16_t Length);
void OnboardLedColour(uint8_t R, uint8_t G, uint8_t B);
*/
#endif
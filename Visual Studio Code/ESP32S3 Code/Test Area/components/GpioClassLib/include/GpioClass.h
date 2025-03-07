#ifndef GpioClass_H
#define GpioClass_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
//#include "led_strip.h"
#include "sdkconfig.h"
#include "soc/rmt_reg.h"

#include "driver/rmt_tx.h"         
#include "driver/rmt_encoder.h"    
       




class GpioClass
{
    private:
        bool IsRuntimeLoggingEnabled = true;

        bool SetupRmtTxChannel(gpio_num_t GpioNumber, uint32_t Resolution);
        void IRAM_ATTR ConvertByteToWS2812b(rmt_symbol_word_t* Symbols, uint8_t Byte);


    protected:



    public:
        GpioClass();
        ~GpioClass();

        bool SetupOnboardLed();
        bool ChangeOnboardLedColour(uint8_t Red, uint8_t Green, uint8_t Blue);

};



/*
static uint8_t LedState = 0;
static led_strip_handle_t LedStrip;

bool SetupOutputPin(uint8_t Pin, gpio_pullup_t PullUp, gpio_pulldown_t PullDown);
bool SetupNeopixel(uint8_t Pin, uint16_t Length);
void OnboardLedColour(uint8_t R, uint8_t G, uint8_t B);
*/
#endif
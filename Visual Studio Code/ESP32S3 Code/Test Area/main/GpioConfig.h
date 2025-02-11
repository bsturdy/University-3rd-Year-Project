#ifndef GpioConfig_H
#define GpioConfig_H

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"

static uint8_t LedState = 0;
static led_strip_handle_t LedStrip;

bool SetupOutputPin(uint8_t Pin, gpio_pullup_t PullUp, gpio_pulldown_t PullDown);
bool SetupNeopixel(uint8_t Pin, uint16_t Length);
void OnboardLedColour(uint8_t R, uint8_t G, uint8_t B);

#endif
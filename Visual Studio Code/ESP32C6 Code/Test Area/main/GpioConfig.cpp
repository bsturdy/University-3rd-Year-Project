#include "GpioConfig.h"



//=============================================================//
//    GPIO                                                     //
//=============================================================// 

static const char *TAG = "GPIO";


bool SetupOutputPin(uint8_t Pin, gpio_pullup_t PullUp, gpio_pulldown_t PullDown)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupOutputPin Executed! Pin Number: %d", Pin);

    gpio_config_t Config =
    {
        .pin_bit_mask = (1ULL << Pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = PullUp,
        .pull_down_en = PullDown,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&Config);

    ESP_LOGI(TAG, "SetupOutputPin Successful!");
    printf("\n");

    return true;
}

bool SetupNeopixel(uint8_t Pin, uint16_t Length)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupNeopixel Executed! Pin Number: %d", Pin);

    led_strip_config_t StripConfig = 
    {
        .strip_gpio_num = Pin,
        .max_leds = Length, 
    };

    led_strip_rmt_config_t RmtConfig = 
    {
        .resolution_hz = 10 * 1000 * 1000,  // 10MHz
        .flags = { .with_dma = false },  // Initialize the flags struct fully
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&StripConfig, &RmtConfig, &LedStrip));

    /* Set all LED off to clear all pixels */
    led_strip_clear(LedStrip);

    ESP_LOGI(TAG, "SetupNeopixel Successful!");
    printf("\n");

    return true;
}

void OnboardLedColour(uint8_t R, uint8_t G, uint8_t B)
{
    // Set the color of the first LED on the strip (0-indexed)
    led_strip_set_pixel(LedStrip, 0, G, R, B); // Set LED at index 0

    // Refresh the strip to apply the new color
    led_strip_refresh(LedStrip);
}
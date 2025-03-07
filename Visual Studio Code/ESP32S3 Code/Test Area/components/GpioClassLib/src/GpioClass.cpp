#include "GpioClass.h"

// Author - Ben Sturdy





//==============================================================================// 
//                                                                              //
//                            GPIO Class                                        //
//                                                                              //
//==============================================================================// 

#define OnboardLedPin           GPIO_NUM_38
#define LedClock                ((uint32_t)40000000)
#define WS2812_TICKS_0_HIGH     16      // 0.4µs
#define WS2812_TICKS_0_LOW      34      // 0.85µs
#define WS2812_TICKS_1_HIGH     34      // 0.8µs
#define WS2812_TICKS_1_LOW      16      // 0.45µs
#define WS2812_RESET_TICKS      2000    // 50µs (ensures reset)

#define TAG                     "GPIO Class"

static GpioClass* ClassInstance;

static rmt_channel_handle_t RmtLedChannel = NULL;
static rmt_encoder_handle_t RmtLedEncoder = NULL;




//==============================================================================//
//                                                                              //
//            Constructors, Destructors, Internal Functions                     //
//                                                                              //
//==============================================================================// 

// Constructor
GpioClass::GpioClass()
{
    ClassInstance = this;
}

// Destructor
GpioClass::~GpioClass()
{
    ;
} 





bool GpioClass::SetupRmtTxChannel(gpio_num_t GpioNumber, uint32_t Resolution) 
{
    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "SETUP RMT TX CHANNEL BEGIN");
    }


    // Create RMT Channel (Automatically Selects A Free Channel)
    rmt_tx_channel_config_t Config = 
    {
        .gpio_num = GpioNumber,             // GPIO to use
        .clk_src = RMT_CLK_SRC_APB,         // Use default clock source
        .resolution_hz = Resolution,        // Set resolution
        .mem_block_symbols = 64,            // Memory block size (adjust if needed)
        .trans_queue_depth = 4,             // Transmission queue depth
        .flags = 
        {
            .with_dma = false,            // No DMA for now
        }
    };
    esp_err_t err = rmt_new_tx_channel(&Config, &RmtLedChannel);
    if (err != ESP_OK) 
    {
        ESP_LOGE("RMT", "Failed to create RMT TX channel: %s", esp_err_to_name(err));
        return false;
    }


    // Enable RMT Channel
    err = rmt_enable(RmtLedChannel);
    if (err != ESP_OK) 
    {
        ESP_LOGE("RMT", "Failed to enable RMT channel: %s", esp_err_to_name(err));
        return false;
    }


    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        ESP_LOGW(TAG, "SETUP RMT TX CHANNEL COMPLETED");
        printf("\n");
    }
    return true;
}


void IRAM_ATTR GpioClass::ConvertByteToWS2812b(rmt_symbol_word_t* Symbols, uint8_t Byte) 
{
    for (int i = 0; i < 8; i++) 
    {
        // If the bit is 1
        if (Byte & (1 << (7 - i)))  
        {
            Symbols[i] = (rmt_symbol_word_t) 
            {
                .duration0 = WS2812_TICKS_1_HIGH,
                .level0 = 1,  // High level
                .duration1 = WS2812_TICKS_1_LOW,
                .level1 = 0   // Low level
            };
        }

        // If the bit is 0
        else 
        {
            Symbols[i] = (rmt_symbol_word_t) 
            {
                .duration0 = WS2812_TICKS_0_HIGH,
                .level0 = 1,  // High level
                .duration1 = WS2812_TICKS_0_LOW,
                .level1 = 0   // Low level
            };
        }
    }
}





//==============================================================================// 
//                                                                              //
//                       Public Setup Functions                                 //
//                                                                              //
//==============================================================================// 

bool GpioClass::SetupOnboardLed()
{
    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "SETUP ONBOARD LED BEGIN");
    }


    if (!ClassInstance->SetupRmtTxChannel(OnboardLedPin, LedClock))
    {
        ESP_LOGE(TAG, "Failed to start RMT channel for onboard LED!");
        return false;
    }

    rmt_bytes_encoder_config_t EncoderConfig = 
    {
        .bit0 = 
        {
            .duration0 = WS2812_TICKS_0_HIGH, 
            .level0 = 1,
            .duration1 = WS2812_TICKS_0_LOW,
            .level1 = 0,
        },
        .bit1 = 
        {
            .duration0 = WS2812_TICKS_1_HIGH,
            .level0 = 1,
            .duration1 = WS2812_TICKS_1_LOW,
            .level1 = 0,
        },
        .flags = 
        {
            .msb_first = 1,
        }
    };

    esp_err_t err = rmt_new_bytes_encoder(&EncoderConfig, &RmtLedEncoder);
    if (err != ESP_OK) 
    {
        ESP_LOGE("RMT", "Failed to create RMT Byte Encoder: %s", esp_err_to_name(err));
        return false;
    }


    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        ESP_LOGW(TAG, "SETUP ONBOARD LED COMPLETED");
        printf("\n");
    }
    return true;
}





//==============================================================================// 
//                                                                              //
//                             Commands                                         //
//                                                                              //
//==============================================================================//

bool GpioClass::ChangeOnboardLedColour(uint8_t Red, uint8_t Green, uint8_t Blue)
{
    uint8_t LedData[3] = {Green, Red, Blue};


    rmt_symbol_word_t ResetData = 
    {
        .duration0 = WS2812_RESET_TICKS,
        .level0 = 0,
        .duration1 = 0,
        .level1 = 0,
    };


    // Send the data
    rmt_transmit_config_t TransmitConfig = 
    {
        .loop_count = 0,
    };
    esp_err_t err = rmt_transmit(RmtLedChannel, RmtLedEncoder, LedData, 24, &TransmitConfig);
    if (err != ESP_OK) 
    {
        ESP_LOGE("RMT", "Failed to transmit WS2812B LED data: %s", esp_err_to_name(err));
        return false;  
    }
    err = rmt_transmit(RmtLedChannel, RmtLedEncoder, &ResetData, 1, &TransmitConfig);
    if (err != ESP_OK) 
    {
        ESP_LOGE("RMT", "Failed to transmit WS2812B reset data: %s", esp_err_to_name(err));
        return false;  
    }


    return true;
}





//==============================================================================// 
//                                                                              //
//                             Get / Set                                        //
//                                                                              //
//==============================================================================//

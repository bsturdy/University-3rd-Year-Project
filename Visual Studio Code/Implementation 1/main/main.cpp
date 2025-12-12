#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "GpioClass.h"
#include "TimerClass.h"
#include "WifiClass.h"
#include "UtilitiesClass.h"

#define TAG "Main"

TimerClass Timer;
GpioClass OnboardLed;
UtilitiesClass Utilities;
WifiClass Wifi;

uint64_t Uid = CONFIG_ESP_NODE_UID;
uint64_t CyclicCalls = 0;
uint8_t CyclicState = 0;
uint8_t MainState = 0;



void CyclicTask1(void* pvParameters)
{
    CyclicCalls++;

    switch(CyclicState)
    {
        case 0:

            CyclicState = 1;
            break;


        case 1:

            CyclicState = 0;
            break;


        default:

            CyclicState = 0;
            break;
    }
};



extern "C" void app_main(void)
{
    while(true)
    {
        switch(MainState)
        {
            case 0:
                OnboardLed.SetupOnboardLed();
                Wifi.SetupWifi(1);
                Wifi.SetupEspNow(1);
                if (Wifi.GetIsConnectedToHost() and Wifi.GetApIpAddress()[0] != '\0')
                {
                    ESP_LOGI(TAG, "Connected to AP with IP: %s", Wifi.GetApIpAddress());
                    OnboardLed.ChangeOnboardLedColour(0, 255, 0);
                    MainState = 1;
                }
                else
                {
                    ESP_LOGI(TAG, "Waiting to connect to AP...");
                    OnboardLed.ChangeOnboardLedColour(255, 165, 0);
                }
                break;
    

            case 1:
                if (Timer.SetupCyclicTask(CyclicTask1, 0))
                {
                    ESP_LOGI(TAG, "Cyclic Task Started");
                    MainState = 2;
                }
                else
                {
                    ESP_LOGE(TAG, "Cyclic Task Failed to Start");
                    MainState = 99;
                }
                break;


            case 2:
                printf("\n\nUptime: %llu ms | Free Heap: %zu bytes | Chip Temp: %.2f C | Cyclic Calls: %llu\n", 
                    Utilities.GetUptimeMs(),
                    Utilities.GetFreeHeapBytes(),
                    Utilities.GetChipTemperatureC(),
                    CyclicCalls
                );
                break;


            case 99:
            OnboardLed.ChangeOnboardLedColour(255, 0, 0);
                break;


            default:
                break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

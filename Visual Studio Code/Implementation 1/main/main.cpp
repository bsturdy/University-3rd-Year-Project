#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "GpioClass.h"
#include "TimerClass.h"
#include "WifiClass.h"
#include "UtilitiesClass.h"
#include "packet_processors.h"
#include <cstdint>

#define TAG "Main"
#define CYCLIC_TAG "CyclicTask"

TimerClass Timer;
GpioClass OnboardLed;
UtilitiesClass Utilities;
//WifiClass Wifi;
Station* WifiSta = nullptr;
Packet1 packet1;
ITF_PacketProcessor* processor = &packet1;

uint64_t Uid = CONFIG_ESP_NODE_UID;
uint8_t MainState = 0;

uint64_t CyclicCalls = 0;
uint8_t CyclicState = 0;
bool DataReady = false;
uint8_t RawUdpData[100]{};
uint8_t ProcessedData[100]{};


void CyclicTask1(void* pvParameters)
{
    CyclicCalls++;

    if (!WifiSta->IsConnectedToHost()) CyclicState = 99;

    switch(CyclicState)
    {
        case 0:
            WifiSta->GetDataFromBuffer(&DataReady, RawUdpData);
            if (DataReady) CyclicState = 1;
            break;


        case 1:
            if (processor->ProcessPacket(RawUdpData, 64, ProcessedData)) CyclicState = 2;
            else CyclicState = 5;
            break;


        case 2:
            ESP_LOGI(CYCLIC_TAG, "Processed payload (bytes):");
            ESP_LOG_BUFFER_HEX(CYCLIC_TAG, ProcessedData, 14);
            DataReady = false;
            CyclicState = 0;
            break;


        default:
            CyclicState = 0;
            break;
    }
};



extern "C" void app_main(void)
{
    WifiSta = WifiFactory::CreateStation(1, 10050, true);


    while(true)
    {
        switch(MainState)
        {
            case 0:
                OnboardLed.SetupOnboardLed();
                WifiSta->SetupWifi();
                if (WifiSta->IsConnectedToHost())
                {
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
                    MainState = 2;
                }
                else
                {
                    ESP_LOGE(TAG, "Failed to start cyclic task!");
                    OnboardLed.ChangeOnboardLedColour(255, 0, 0);
                }

            case 2:
                printf("\n\nUptime: %llu ms | Free Heap: %zu bytes | Chip Temp: %.2f C | Cyclic Calls: %llu | Cyclic State: %i | GW Address: %s | My Address: %s\n", 
                    Utilities.GetUptimeMs(),
                    Utilities.GetFreeHeapBytes(),
                    Utilities.GetChipTemperatureC(),
                    CyclicCalls,
                    CyclicState,
                    WifiSta->GetGatewayIpAddress(),
                    WifiSta->GetMyIpAddress()
                );
                printf("\nSlots Filled: %i\n\n",
                    WifiSta->GetNumberOfPacketsInBuffer());
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

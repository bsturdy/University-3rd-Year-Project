#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "WifiClass.h"
#include "TimerClass.h"
#include "GpioConfig.h"
#include <cmath>





//==============================================================================//
//                                                                              //
//                               Main Program                                   //
//                                                                              //
//==============================================================================// 

#define TAG     "Main"

// Wifi
WifiClass Wifi;

// Hardware Timer
TimerClass Timer;

// Main Task
TaskHandle_t MainHandle;



// Cyclic task
uint64_t CyclicCounter = 1;
uint8_t CyclicState = 0;

void CyclicUserTaskAp(void* pvParameters)
{
    if (CyclicCounter > 1000)
    {
        CyclicCounter = 0;
    }

    switch (CyclicState)
    {
        case 0:
            break;

        default:
            break;
    }
    
    CyclicCounter++;
}



bool WifiConnectionTrigger = true;
uint64_t WifiConnectionCounter = 0;
void CyclicUserTaskStation(void* pvParameters)
{
    if (CyclicCounter > 1000)
    {
        CyclicCounter = 0;
    }

    switch (CyclicState)
    {
        case 0:
            if (Wifi.GetIsConnectedToHost() && WifiConnectionTrigger)
            {
                WifiConnectionCounter = WifiConnectionCounter + 1;
                WifiConnectionTrigger = false;
            }
            else if (!Wifi.GetIsConnectedToHost())
            {
                WifiConnectionTrigger = true;
            }
            break;
    

        default:
            break;
    }

    CyclicCounter = CyclicCounter + 1;
}



void Main(void* pvParameters)
{
    uint64_t CyclicIsr = 0;
    uint64_t CyclicTask = 0;
    uint64_t WatchdogIsr = 0;
    uint64_t WatchdogTask = 0;
    uint64_t CyclicIsrPrev = 0;
    uint64_t CyclicTaskPrev = 0;
    uint64_t WatchdogIsrPrev = 0;
    uint64_t WatchdogTaskPrev = 0;
    uint64_t MainCounter = 1;
    uint64_t TimeAliveInSeconds = 0;
    while(1)
    {

        // Main printing info
        if (MainCounter == 10)
        {
            CyclicIsr = Timer.GetCyclicIsrCounter();
            CyclicTask = Timer.GetCyclicTaskCounter();
            WatchdogIsr = Timer.GetWatchdogIsrCounter();
            WatchdogTask = Timer.GetWatchdogTaskCounter();

            printf("\n\n=============================================\n");
            printf(" %llu", TimeAliveInSeconds);
            printf("               Counters                    \n");
            printf("    Cyclic ISR:                  %llu\n", (CyclicIsr - CyclicIsrPrev));
            printf("    Cyclic Task:                 %llu\n", (CyclicTask - CyclicTaskPrev));        
            printf("    Watchdog ISR:                %llu\n", (WatchdogIsr - WatchdogIsrPrev));
            printf("    Watchdog Task:               %llu\n\n", (WatchdogTask - WatchdogTaskPrev));
            if (Wifi.GetIsAp())
            {
                printf("    Number of devices connected: %d\n", Wifi.GetNumClientsConnected());
            }
            if (Wifi.GetIsSta())
            {
                printf("    Is connected to host:        %d\n", Wifi.GetIsConnectedToHost());
                printf("    Access Point IP:             %s\n", Wifi.GetApIpAddress());
            }
            printf("=============================================\n");

            CyclicIsrPrev = CyclicIsr;
            CyclicTaskPrev = CyclicTask;
            WatchdogIsrPrev = WatchdogIsr;
            WatchdogTaskPrev = WatchdogTask;

            MainCounter = 0;

            TimeAliveInSeconds++;
        }

        if (MainCounter % 2 == 0)
        {
            ;//ESP_LOGE(TAG, "Connection Count: %llu", WifiConnectionCounter);
        }
        MainCounter++;

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}



extern "C" void app_main()
{ 
    esp_log_level_set("*", ESP_LOG_INFO);

    if (!Wifi.SetupWifi(0))
    {
        ESP_LOGE(TAG, "SetupWifi Failed!");
        return;
    }
    if (!Wifi.SetupEspNow(0))
    {
        ESP_LOGE(TAG, "SetupEspNow Failed!");
        return;
    }
    if (Wifi.GetIsAp())
    {
        if (!Timer.SetupCyclicTask(CyclicUserTaskAp, 1))
        {
            ESP_LOGE(TAG, "SetupCyclicTask AP Failed!");
            return;
        }
    }
    else if (Wifi.GetIsSta())
    {
        if (!Timer.SetupCyclicTask(CyclicUserTaskStation, 1))
        {
            ESP_LOGE(TAG, "SetupCyclicTask Station Failed!");
            return;
        }
    }
    if (xTaskCreatePinnedToCore(Main, "Main Task", 4096, NULL, 1, &MainHandle, 0) != pdPASS)
    {
        ESP_LOGE(TAG, "SetupMainTask Failed!");
        return;
    }
}
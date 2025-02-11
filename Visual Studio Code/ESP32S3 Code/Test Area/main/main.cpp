

//=============================================================//
//    Main Program                                             //
//=============================================================// 

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "WifiClass.h"
#include "TimerClass.h"
#include "GpioConfig.h"
#include <cmath>

static const char *TAG = "Main";

// Wifi
WifiClass Wifi;
constexpr bool IsAccessPoint = true;


// Hardware Timer
TimerClass Timer;
float CycleTime = 0.05;
float WatchdogTime = 0.04;
uint16_t Prescalar = 2;

uint64_t CyclicIsr = 0;
uint64_t CyclicTask = 0;
uint64_t WatchdogIsr = 0;
uint64_t WatchdogTask = 0;
uint64_t CyclicIsrPrev = 0;
uint64_t CyclicTaskPrev = 0;
uint64_t WatchdogIsrPrev = 0;
uint64_t WatchdogTaskPrev = 0;
uint64_t MainCounter = 0;

TaskHandle_t MainHandle;
void Main(void* pvParameters)
{
    while(1)
    {
        CyclicIsr = Timer.GetCyclicIsrCounter();
        CyclicTask = Timer.GetCyclicTaskCounter();
        WatchdogIsr = Timer.GetWatchdogIsrCounter();
        WatchdogTask = Timer.GetWatchdogTaskCounter();

        printf("\n\n=============================================\n");
        printf(" %llu", MainCounter);
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
        }
        printf("=============================================\n");

        CyclicIsrPrev = CyclicIsr;
        CyclicTaskPrev = CyclicTask;
        WatchdogIsrPrev = WatchdogIsr;
        WatchdogTaskPrev = WatchdogTask;

        MainCounter++;

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

uint64_t CyclicCounter = 0;
uint8_t CyclicState = 0;
void CyclicUserTaskAp(void* pvParameters)
{
    if (CyclicCounter >= 100)
    {
        CyclicCounter = 0;
    }

    switch (CyclicState)
    {
        case 0:
            if (CyclicCounter == 0)
            {
                //OnboardLedColour(120, 60, 0);
            }
            if (CyclicCounter == 500)
            {
                //OnboardLedColour(0, 0, 0);
            }
            break;

        case 1:
            break;

        case 2:
            break;

        case 3:
            break;
    }

    CyclicCounter++;
}
void CyclicUserTaskSta(void* pvParameters)
{
    if (CyclicState == 0)
    {
        if (Wifi.GetIsConnectedToHost())
        {
            CyclicState = 1;
        }
    }
    else
    {
        if (!Wifi.GetIsConnectedToHost())
        {
            CyclicState = 0;
        }
    }
    

    switch (CyclicState)
    {
        case 0:
            break;

        case 1:

            if (CyclicCounter == 0)
            {
                Wifi.SendUdpPacket("Test Packet", "192.168.4.1", 25000);
            }
            break;

        case 2:
            break;

        case 3:
            break;
    }

    CyclicCounter++;
    if (CyclicCounter >= 200)
    {
        CyclicCounter = 0;
    }
}


extern "C" void app_main()
{ 
    if (IsAccessPoint)
    {
        ESP_LOGI(TAG, "Configuring Access Point!");
        if (!Wifi.SetupWifiAP(25000, 10, 0))
        {
            ESP_LOGE(TAG, "SetupWifiAP Failed!");
            return;
        }
        if (!Wifi.SetupEspNow(0))
        {
            ESP_LOGE(TAG, "SetupEspNow Failed!");
            return;
        }
        if (!Timer.SetupCyclicTask(CyclicUserTaskAp, CycleTime, WatchdogTime, 2, 1))
        {
            ESP_LOGE(TAG, "SetupCyclicTask Failed!");
            return;
        }
        if (xTaskCreatePinnedToCore(Main, "Main Task", 4096, NULL, 1, &MainHandle, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "SetupMainTask Failed!");
            return;
        }
    }
    
    else
    {
        ESP_LOGI(TAG, "Configuring Station!");
        if (!Wifi.SetupWifiSta(25000, 10, 0))
        {
            ESP_LOGE(TAG, "SetupWifiSta Failed!");
            return;
        }
        if (!Wifi.SetupEspNow(0))
        {
            ESP_LOGE(TAG, "SetupEspNow Failed!");
            return;
        }
        if (!Timer.SetupCyclicTask(CyclicUserTaskSta, CycleTime, WatchdogTime, 2, 1))
        {
            ESP_LOGE(TAG, "SetupCyclicTask Failed!");
            return;
        }
        if (xTaskCreatePinnedToCore(Main, "Main Task", 2048, NULL, 1, &MainHandle, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "SetupMainTask Failed!");
            return;
        }
    }
}


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
bool IsAccessPoint = true;


// Hardware Timer
TimerClass Timer;
float CycleTime = 1;
float WatchdogTime = 0.7;
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
        printf("    Number of devices connected: %d\n", Wifi.GetNumClientsConnected());
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
    if (CyclicCounter >= 1000)
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
            if (CyclicCounter % 1 == 0)
            {
                //OnboardLedColour(0, 0, 0);
            }
            if (CyclicCounter % 3 == 0)
            {
                //OnboardLedColour(0, 0, 120);
            }
            break;

        case 1:
            //OnboardLedColour(0, 0, 120);
            break;

        case 2:
            break;

        case 3:
            break;
    }

    CyclicCounter++;
}


extern "C" void app_main()
{ 
    uint8_t State = 0;
        
    while(true)
    {
        switch (State)
        {
            case 0:
                if (IsAccessPoint)
                {
                    if (Wifi.SetupWifiAP(25000, 100))
                    {
                        State = 1;
                    }
                    else
                    {
                        State = 99;
                    }
                }
                else
                {
                    if (Wifi.SetupWifiSta(25000))
                    {
                        State = 1;
                    }
                    else
                    {
                        State = 99;
                    }
                }
                break;

            case 1:
                if (Wifi.SetupEspNow())
                {
                    State = 2;
                }
                else
                {
                    State = 99;
                }
                break;

            case 2:
                //if (SetupNeopixel(8, 1))
                //{
                    State = 3;
                //}
                break;

            case 3:
                if (IsAccessPoint)
                {
                    if (Timer.SetupCyclicTask(CyclicUserTaskAp, CycleTime, WatchdogTime, 2))
                    {
                        State = 4;
                    }
                    else
                    {
                        State = 99;
                    }
                }
                else
                {
                    if (Timer.SetupCyclicTask(CyclicUserTaskSta, CycleTime, WatchdogTime, 2))
                    {
                        State = 4;
                    }
                    else
                    {
                        State = 99;
                    }
                }

                break;

            case 4:
                xTaskCreate(Main, "Main Task", 2048, NULL, 1, &MainHandle);
                vTaskDelete(NULL);
                break;

            case 99:
                ESP_LOGE(TAG, "System Error");
                vTaskDelete(NULL);
                break;
        }
    }
}
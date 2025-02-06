

//=============================================================//
//    Main Program                                             //
//=============================================================// 

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "WifiClass.h"
#include "TimerClass.h"
#include "GpioConfig.h"
#include <cmath>


// Wifi
WifiClass WifiAp;


// Hardware Timer
TimerClass Timer;
float CycleTime = 1;
float WatchdogTime = 0.8;
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
        printf("    Number of devices connected: %d\n", WifiAp.GetNumClientsConnected());
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
void CyclicUserTask(void* pvParameters)
{
    if (CyclicCounter >= 1000)
    {
        CyclicCounter = 0;
    }

    if (CyclicCounter == 0)
    {
        OnboardLedColour(120, 60, 0);
    }

    if (CyclicCounter == 500)
    {
        OnboardLedColour(0, 0, 0);
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
                if (WifiAp.SetupWifiAP())
                {
                    State = 1;
                }
                break;

            case 1:
                if (WifiAp.SetupEspNow())
                {
                    State = 2;
                }
                break;

            case 2:
                if (SetupNeopixel(8, 1))
                {
                    State = 3;
                }
                break;

            case 3:
                if (Timer.SetupCyclicTask(CyclicUserTask, CycleTime, WatchdogTime, 2))
                {
                    State = 4;
                }
                break;

            case 4:
                xTaskCreate(Main, "Main Task", 2048, NULL, 1, NULL);
                vTaskDelete(NULL);
                break;
        }
    }
}
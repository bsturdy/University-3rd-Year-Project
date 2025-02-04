

//=============================================================//
//    Main Program                                             //
//=============================================================// 


#include "WifiClass.h"
#include "TimerClass.h"
#include <cmath>


// Wifi
WifiClass WifiAp;


// Hardware Timer
TimerClass Timer;
float CycleTime = 0.25;
float WatchdogTime = 0.225;
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

static uint64_t FibIn = 1350;
static uint64_t FibOut;

uint64_t Fibonacci(uint64_t n) {
    if (n <= 1) {
        return n; // Base cases: Fibonacci(0) = 0, Fibonacci(1) = 1
    }

    uint64_t a = 0, b = 1, fib = 0;
    for (uint64_t i = 2; i <= n; ++i) {
        fib = a + b; // Calculate the next Fibonacci number
        a = b;       // Shift values for the next iteration
        b = fib;
    }
    return fib;
}


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


void DeterministicTask(void* pvParameters)
{
    FibOut = Fibonacci(uint64_t(FibIn));
}


extern "C" void app_main()
{
    //WifiAp.SetupWifiAP();

    Timer.SetupCyclicTask(DeterministicTask, CycleTime, WatchdogTime, 2);

    xTaskCreate(Main, "Main Task", 2048, NULL, 1, NULL);
}


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
float CycleTime = 100;
uint16_t Prescalar = 2;
uint64_t CurrentCounterIsr = 0;
uint64_t CurrentCounterTask = 0;
uint64_t PreviousCounterIsr = 0;
uint64_t PreviousCounterTask = 0;
uint64_t ExpectedValue = 0;
uint64_t ActualValue = 0;
uint64_t TimerFrequency = 0;
double Error = 0;


void Main(void* pvParameters)
{
    while(1)
    {
        CurrentCounterIsr = Timer.GetIsrCounter();
        CurrentCounterTask = Timer.GetTaskCounter();
        TimerFrequency = Timer.GetTimerFrequency();
        ExpectedValue = CurrentCounterIsr - PreviousCounterIsr;
        ActualValue = CurrentCounterTask - PreviousCounterTask;
        Error = (100 * (double)(std::abs((int64_t)ExpectedValue - (int64_t)ActualValue))) / (double)ExpectedValue;
        PreviousCounterIsr = CurrentCounterIsr;
        PreviousCounterTask = CurrentCounterTask;

        printf("===============================================\n");
        printf("    Timer Frequency (Hz):        %llu\n", TimerFrequency);
        printf("    Cycle Time (ms):             %f\n", CycleTime);        
        printf("    Expected Cycle Count (/s):   %llu\n", ExpectedValue);
        printf("    Actual Cycle Count (/s):     %llu\n", ActualValue);
        printf("    Error (%%):                   %f\n\n", Error);
        printf("    Number of devices connected: %d\n", WifiAp.GetNumClientsConnected());
        printf("===============================================\n\n\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}


void DeterministicTask(void* pvParameters)
{
    printf("Testing\n\n");
}


extern "C" void app_main()
{
    WifiAp.SetupWifiAP();

    Timer.SetupDeterministicTask(DeterministicTask, 2048);

    Timer.SetupTimer(CycleTime, Prescalar);

    xTaskCreate(Main, "Main Task", 2048, NULL, 1, NULL);
}
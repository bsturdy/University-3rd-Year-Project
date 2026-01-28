#ifndef TimerClass_H
#define TimerClass_H

#include "driver/timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

class TimerClass
{
    public:
        // Singleton Accessor
        static TimerClass& GetInstance();
        TimerClass(const TimerClass&) = delete;
        void operator=(const TimerClass&) = delete;

        // Setup methods
        bool SetupCyclicTask(void (*TaskToRun)(void*), uint8_t CoreToUse);
        void SetWatchdogOnOff(bool IsWatchdogEnabled);

        // Getters
        uint64_t GetCyclicIsrCounter() const { return CyclicIsrCounter; }
        uint64_t GetCyclicTaskCounter() const { return CyclicTaskCounter; }
        uint64_t GetWatchdogIsrCounter() const { return WatchdogISRCounter; }
        uint64_t GetWatchdogTaskCounter() const { return WatchdogTaskCounter; }
        uint64_t GetTimerFrequency() const { return 80000000.0 / Prescalar; }
        
        TaskHandle_t GetCyclicTaskHandle() const { return CyclicTaskHandle; }
        TaskHandle_t GetWatchdogTaskHandle() const { return WatchdogTaskHandle; }

    private:
        // Private Constructor
        TimerClass();
        ~TimerClass();

        // Helper to setup hardware timers
        bool SetupTimer(float CycleTimeInMs, float WatchdogTime, uint16_t Prescalar);
        bool IsSetupDone;

        // Static ISRs & Tasks 
        static bool IRAM_ATTR CyclicISR(void* arg);
        static bool IRAM_ATTR WatchdogISR(void* arg);
        static void CyclicTask(void *pvParameters);
        static void WatchdogTask(void *pvParameters);

        // Instance Variables
        static const int CyclicTaskStackSize = 4096;
        StackType_t CyclicTaskStack[CyclicTaskStackSize];
        StaticTask_t CyclicTaskTCB;
        
        static const int WatchdogTaskStackSize = 1024;
        StackType_t WatchdogTaskStack[WatchdogTaskStackSize];
        StaticTask_t WatchdogTaskTCB;

        volatile bool AreTimersInitated = false;
        volatile uint64_t CyclicIsrCounter = 0;
        volatile uint64_t CyclicTaskCounter = 0;
        volatile uint64_t WatchdogISRCounter = 0;
        volatile uint64_t WatchdogTaskCounter = 0;

        TaskHandle_t CyclicTaskHandle = NULL;
        TaskHandle_t WatchdogTaskHandle = NULL;
        
        // Notification objects
        BaseType_t xHigherPriorityTaskWokenFalse = pdFALSE;
        BaseType_t xHigherPriorityTaskWokenTrue = pdTRUE;

        void (*UserTask)(void*) = nullptr;
        
        float CycleTimeMs = 0;
        float WatchdogTimeMs = 0;
        uint16_t Prescalar = 1;
        bool IsWatchdogEnabled = true;
        uint8_t CoreToRunCyclicTask = 1;

        // This static pointer holds the address of the instance specifically for ISR access.
        static TimerClass* isr_instance; 
};

#endif
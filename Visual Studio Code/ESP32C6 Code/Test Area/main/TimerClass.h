#ifndef TimerClass_H
#define TimerClass_H

#include "driver/timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

class TimerClass
{
    private:
        float CycleTimeMs;                                          // Speed that the cyclic deterministic task will run at
        float MaxExecutionTimeMs;                                   // Time that the task has to run before the timout occurs, saving the system from exceeding
        uint16_t Prescalar;                                         // Timer clock ticks are scaled by this, e.g. 1000Hz clock with 5 prescalar = 200Hz timer (5 clock ticks per 1 timer tick)

        static const int CyclicStackSize = 2048;                    // Size given to cyclic task
        static StackType_t CyclicTaskStack[CyclicStackSize];        // Static memory size for cyclic task
        static StaticTask_t CyclicTaskTCB;                          // Static memory location for cyclic task
        static const int WatchdogStackSize = 256;                   // Size given to watchdog task (minimum 256)
        static StackType_t WatchdogTaskStack[WatchdogStackSize];    // Static memory size for watchdog task  
        static StaticTask_t WatchdogTaskTCB;                        // Static memory location for watchdog task

        volatile uint64_t CyclicIsrCounter = 0;                     // Number of times cyclic ISR is triggered
        volatile uint64_t CyclicTaskCounter = 0;                    // Number of times task from cyclic ISR is triggered
        volatile uint64_t WatchdogISRCounter = 0;                   // Number of times task from cyclic ISR exceeds its maximum execution time
        volatile uint64_t WatchdogTaskCounter = 0;                  // Number of times Watchdog reset task is triggered

        volatile bool AreTimersInitated = false;                    // Internal check
        bool IsWatchdogEnabled = true;                              // Internal check

        void (*UserTask)(void*);                                    // Pointer to task to be ran in the deterministic environment
        static bool IRAM_ATTR CyclicISR(void* arg);                 // ISR called cyclically by a hardware timer, signals deterministic task to run
        static bool IRAM_ATTR WatchdogISR(void* arg);               // ISR called by a timer triggered in the cyclic task, signals that a task has taken too long and must be terminated
        static void CyclicTask(void *pvParameters);                 // Task called by cyclic ISR. This task calls the user task
        static void WatchdogTask(void *pvParameters);               // Task called by Watchdog ISR. This task resets the cyclic task

        void SetupTimer(float CycleTimeInMs, float WatchdogTime, 
                        uint16_t Prescalar);                        // Configures cyclic timer at 80MHz to call cyclic ISR



    protected:



    public:
        TimerClass();
        ~TimerClass();

        void SetupCyclicTask(void (*TaskToRun)(void*), float CycleTimeInMs, 
                            float WatchdogTime, uint16_t Prescalar);            // Takes user-input task and assigns it to UserTask pointer
        uint64_t GetCyclicIsrCounter();
        uint64_t GetCyclicTaskCounter();
        uint64_t GetWatchdogIsrCounter();
        uint64_t GetWatchdogTaskCounter();
        uint64_t GetTimerFrequency();
        void SetWatchdogOnOff(bool IsWatchdogEnabled);                          // Determines if the Watchdog functionality is used or not 
};


#endif
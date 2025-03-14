#ifndef TimerClass_H
#define TimerClass_H

// Author - Ben Sturdy
// This file implements a class 'Timer Class'. This class should be instantiated
// only once in a project. This class configures 2 timers to work together to run
// a task in a cyclic mode. One timer calls a task to run from a hardware ISR  
// cyclically. Another task calls a watchdog timeout system that checks if the cyclic 
// task has exceeded a set limit - useful for ensuring that the idle tasks and / or 
// other tasks continue to run without being drained of CPU resources. This system 
// can be pinned to a core, and is recommended to be pinned to a core with no other 
// processes running on it.

#include "driver/timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

class TimerClass
{
    private:    
        static bool IRAM_ATTR CyclicISR(void* arg);                     // ISR called cyclically by a hardware timer, signals deterministic task to run
        static bool IRAM_ATTR WatchdogISR(void* arg);                   // ISR called by a timer triggered in the cyclic task, signals that a task has taken too long and must be terminated
        static void CyclicTask(void *pvParameters);                     // Task called by cyclic ISR. This task calls the user task
        static void WatchdogTask(void *pvParameters);                   // Task called by Watchdog ISR. This task resets the cyclic task

        static const int CyclicTaskStackSize = 4096;                    // Size given to cyclic task
        static StackType_t CyclicTaskStack[CyclicTaskStackSize];        // Static memory size for cyclic task
        static StaticTask_t CyclicTaskTCB;                              // Static memory location for cyclic task
        
        static const int WatchdogTaskStackSize = 1024;                  // Size given to watchdog task (minimum 256)
        static StackType_t WatchdogTaskStack[WatchdogTaskStackSize];    // Static memory size for watchdog task  
        static StaticTask_t WatchdogTaskTCB;                            // Static memory location for watchdog task

        volatile bool AreTimersInitated = false;                        // Internal check
        volatile uint64_t CyclicIsrCounter = 0;                         // Number of times cyclic ISR is triggered
        volatile uint64_t CyclicTaskCounter = 0;                        // Number of times task from cyclic ISR is triggered
        volatile uint64_t WatchdogISRCounter = 0;                       // Number of times task from cyclic ISR exceeds its maximum execution time
        volatile uint64_t WatchdogTaskCounter = 0;                      // Number of times Watchdog reset task is triggered

        TaskHandle_t CyclicTaskHandle = NULL;                           // Task handle
        TaskHandle_t WatchdogTaskHandle = NULL;                         // Task handle
        BaseType_t xHigherPriorityTaskWokenFalse = pdFALSE;             // Notification Priority Takeover
        BaseType_t xHigherPriorityTaskWokenTrue = pdTRUE;               // Notification Priority Takeover

        void (*UserTask)(void*);                                        // Pointer to task to be ran in the cyclic environment
        bool SetupTimer(float CycleTimeInMs, float WatchdogTime, 
                        uint16_t Prescalar);                            // Configures cyclic timer at 80MHz to call cyclic ISR

        float CycleTimeMs;                                              // Speed that the cyclic deterministic task will run at
        float WatchdogTimeMs;                                           // Time that the task has to run before the timout occurs, saving the system from exceeding
        uint16_t Prescalar;                                             // Timer clock ticks are scaled by this, e.g. 1000Hz clock with 5 prescalar = 200Hz timer (5 clock ticks per 1 timer tick)
        bool IsWatchdogEnabled = true;                                  // Internal check
        uint8_t CoreToRunCyclicTask = 1;                                // Core cyclic task is pinned to, default is core 1



    protected:



    public:
        TimerClass();
        ~TimerClass();

        bool SetupCyclicTask(void (*TaskToRun)(void*), 
                            uint8_t CoreToUse);                         // Takes a task input and converts it into a cyclically executed task with watchdog protection
        void SetWatchdogOnOff(bool IsWatchdogEnabled);                  // Determines if the Watchdog functionality is used or not 
        uint64_t GetCyclicIsrCounter();
        uint64_t GetCyclicTaskCounter();
        uint64_t GetWatchdogIsrCounter();
        uint64_t GetWatchdogTaskCounter();
        uint64_t GetTimerFrequency();
        TaskHandle_t GetCyclicTaskHandle();
        TaskHandle_t GetWatchdogTaskHandle();
};


#endif
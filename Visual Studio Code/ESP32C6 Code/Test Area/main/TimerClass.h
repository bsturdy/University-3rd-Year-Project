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
        float CycleTimeMs;
        uint16_t Prescalar;

        volatile uint64_t TaskCounter = 0;
        volatile uint64_t IsrCounter = 0;

        void (*UserTask)(void*);
        static bool IRAM_ATTR CyclicTimerISR(void* arg);
        static bool IRAM_ATTR TimeoutTimerISR(void* arg);
        static void DeterministicTask(void *pvParameters); 

    public:
        TimerClass();
        ~TimerClass();

        // Timer Functions
        bool SetupTimer(float CycleTimeInMs, uint16_t Prescalar);
        void SetupDeterministicTask(void (*TaskToRun)(void*), uint32_t StackDepth);
        uint64_t GetIsrCounter();
        uint64_t GetTaskCounter();
        uint64_t GetTimerFrequency();
};


#endif
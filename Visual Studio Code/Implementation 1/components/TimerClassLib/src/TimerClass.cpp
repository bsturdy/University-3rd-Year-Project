#include "TimerClass.h"

// Hardware Config Macros
#define CyclicTimerGroup        TIMER_GROUP_0
#define CyclicTimerIndex        TIMER_0
#define WatchdogTimerGroup      TIMER_GROUP_1
#define WatchdogTimerIndex      TIMER_0
#define CyclicPeriodInUs        CONFIG_ESP_CYCLIC_TASK_PERIOD
#define WatchdogPeriodInUs      CONFIG_ESP_WATCHDOG_TASK_PERIOD
#define Prescaler               CONFIG_ESP_TIMER_PRESCALER
#define TAG                     "TimerClass"

#ifndef CONFIG_ESP_CYCLIC_TASK_PERIOD
#define CONFIG_ESP_CYCLIC_TASK_PERIOD 1000
#endif

// Static ISR instance pointer
TimerClass* TimerClass::isr_instance = nullptr;

//==============================================================================//
//                              Constructor / Setup                             //
//==============================================================================//

TimerClass& TimerClass::GetInstance()
{
    static TimerClass instance;
    return instance;
}

TimerClass::TimerClass()
{
    isr_instance = this;

    CycleTimeMs = CyclicPeriodInUs / 1000.0;
    WatchdogTimeMs = WatchdogPeriodInUs / 1000.0;
    Prescalar = Prescaler;
}

TimerClass::~TimerClass()
{
    isr_instance = nullptr;
}

void TimerClass::SetupScopePins()
{
    if (ScopePinsInitialised) return;

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << CyclicProbePin) | (1ULL << WatchdogProbePin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    SetCyclicProbeLow();
    SetWatchdogProbeLow();

    ScopePinsInitialised = true;
}

//==============================================================================//
//                                   ISRs                                       //
//==============================================================================//

bool IRAM_ATTR TimerClass::CyclicISR(void* arg)
{
    if (isr_instance == nullptr) return false;

    isr_instance->CyclicIsrCounter++;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (isr_instance->CyclicTaskHandle != NULL)
    {
        vTaskNotifyGiveFromISR(isr_instance->CyclicTaskHandle, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    return true;
}

bool IRAM_ATTR TimerClass::WatchdogISR(void* arg)
{
    if (isr_instance == nullptr) return false;

    isr_instance->WatchdogISRCounter++;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (isr_instance->WatchdogTaskHandle != NULL)
    {
        vTaskNotifyGiveFromISR(isr_instance->WatchdogTaskHandle, &xHigherPriorityTaskWoken);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    return true;
}

//==============================================================================//
//                                   Tasks                                      //
//==============================================================================//

void TimerClass::CyclicTask(void* pvParameters)
{
    TimerClass* self = (TimerClass*)pvParameters;

    while (true)
    {
        // Wait for the fixed cyclic timer release
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (self->AreTimersInitated && self->UserTask != nullptr)
        {
            // Start cyclic pulse at release instant
            SetCyclicProbeHigh();

            // Reset and start watchdog for this cycle
            timer_set_counter_value(WatchdogTimerGroup, WatchdogTimerIndex, 0);

            if (self->IsWatchdogEnabled)
            {
                timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_EN);
                timer_start(WatchdogTimerGroup, WatchdogTimerIndex);
            }

            // Run user task
            self->UserTask(NULL);
            self->CyclicTaskCounter++;

            // If we got here, task completed before watchdog recovery deleted it
            timer_pause(WatchdogTimerGroup, WatchdogTimerIndex);
            SetCyclicProbeLow();
        }
    }
}

void TimerClass::WatchdogTask(void* pvParameters)
{
    TimerClass* self = (TimerClass*)pvParameters;

    while (true)
    {
        // Wait until watchdog ISR signals timeout
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (self->AreTimersInitated)
        {
            // Mark watchdog recovery active
            SetWatchdogProbeHigh();

            // Force cyclic pulse low immediately because the cyclic task is being aborted
            SetCyclicProbeLow();

            // Stop watchdog timer while handling recovery
            timer_pause(WatchdogTimerGroup, WatchdogTimerIndex);

            // Delete current cyclic task
            if (self->CyclicTaskHandle != NULL)
            {
                vTaskDelete(self->CyclicTaskHandle);
                self->CyclicTaskHandle = NULL;
            }

            // Recreate cyclic task, but DO NOT notify it immediately
            // It must wait for the next normal cyclic timer release
            self->CyclicTaskHandle = xTaskCreateStaticPinnedToCore
            (
                CyclicTask,
                "Deterministic Task",
                CyclicTaskStackSize,
                self,
                configMAX_PRIORITIES - 2,
                self->CyclicTaskStack,
                &self->CyclicTaskTCB,
                self->CoreToRunCyclicTask
            );

            // Re-arm watchdog for the next cycle
            timer_set_counter_value(WatchdogTimerGroup, WatchdogTimerIndex, 0);

            if (self->IsWatchdogEnabled)
            {
                timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_EN);
            }

            self->WatchdogTaskCounter++;

            // Recovery complete
            SetWatchdogProbeLow();
        }
    }
}

//==============================================================================//
//                              Public Setup                                    //
//==============================================================================//

bool TimerClass::SetupCyclicTask(void (*TaskToRun)(void*), uint8_t CoreToUse)
{
    if (this->IsSetupDone) return true;

    ESP_LOGI(TAG, "Setting up Cyclic Task");

    this->CoreToRunCyclicTask = CoreToUse;
    this->UserTask = TaskToRun;

    SetupScopePins();

    // Create cyclic task
    this->CyclicTaskHandle = xTaskCreateStaticPinnedToCore
    (
        CyclicTask,
        "Deterministic Task",
        CyclicTaskStackSize,
        this,
        configMAX_PRIORITIES - 2,
        this->CyclicTaskStack,
        &this->CyclicTaskTCB,
        CoreToRunCyclicTask
    );

    // Create watchdog recovery task
    this->WatchdogTaskHandle = xTaskCreateStaticPinnedToCore
    (
        WatchdogTask,
        "Watchdog Task",
        WatchdogTaskStackSize,
        this,
        configMAX_PRIORITIES - 1,
        this->WatchdogTaskStack,
        &this->WatchdogTaskTCB,
        CoreToRunCyclicTask
    );

    bool Success = SetupTimer(CycleTimeMs, WatchdogTimeMs, Prescalar);

    if (Success) this->IsSetupDone = true;
    return Success;
}

bool TimerClass::SetupTimer(float CycleTimeInMs, float WatchdogTime, uint16_t Prescalar)
{
    ESP_LOGI(TAG, "SetupTimer Executed!");

    this->CycleTimeMs = CycleTimeInMs;
    this->WatchdogTimeMs = WatchdogTime;
    this->Prescalar = Prescalar;

    if (this->CyclicTaskHandle == NULL)
    {
        ESP_LOGE(TAG, "CyclicTaskHandle is NULL. Call SetupCyclicTask first.");
        return false;
    }

    // Configure cyclic timer
    timer_config_t CyclicConfig = {
        .alarm_en = TIMER_ALARM_DIS,
        .counter_en = TIMER_START,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .clk_src = TIMER_SRC_CLK_APB,
        .divider = (uint32_t)this->Prescalar
    };

    ESP_ERROR_CHECK(timer_init(CyclicTimerGroup, CyclicTimerIndex, &CyclicConfig));

    uint64_t cyclic_alarm_val = (80000000.0 / this->Prescalar) * (this->CycleTimeMs / 1000.0);
    ESP_ERROR_CHECK(timer_set_alarm_value(CyclicTimerGroup, CyclicTimerIndex, cyclic_alarm_val));
    ESP_ERROR_CHECK(timer_enable_intr(CyclicTimerGroup, CyclicTimerIndex));
    ESP_ERROR_CHECK(timer_isr_callback_add(CyclicTimerGroup, CyclicTimerIndex, CyclicISR, NULL, ESP_INTR_FLAG_IRAM));
    ESP_ERROR_CHECK(timer_start(CyclicTimerGroup, CyclicTimerIndex));
    ESP_ERROR_CHECK(timer_set_alarm(CyclicTimerGroup, CyclicTimerIndex, TIMER_ALARM_EN));

    // Configure watchdog timer
    timer_config_t WatchdogConfig = {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_DIS,
        .clk_src = TIMER_SRC_CLK_APB,
        .divider = (uint32_t)this->Prescalar
    };

    ESP_ERROR_CHECK(timer_init(WatchdogTimerGroup, WatchdogTimerIndex, &WatchdogConfig));

    uint64_t watchdog_alarm_val = (80000000.0 / this->Prescalar) * (this->WatchdogTimeMs / 1000.0);
    ESP_ERROR_CHECK(timer_set_alarm_value(WatchdogTimerGroup, WatchdogTimerIndex, watchdog_alarm_val));
    ESP_ERROR_CHECK(timer_enable_intr(WatchdogTimerGroup, WatchdogTimerIndex));
    ESP_ERROR_CHECK(timer_isr_callback_add(WatchdogTimerGroup, WatchdogTimerIndex, WatchdogISR, NULL, ESP_INTR_FLAG_IRAM));

    this->AreTimersInitated = true;

    ESP_LOGI(TAG, "SetupTimer Successful! Cyclic Alarm: %llu, Watchdog Alarm: %llu",
             cyclic_alarm_val, watchdog_alarm_val);

    return true;
}

void TimerClass::SetWatchdogOnOff(bool Enabled)
{
    this->IsWatchdogEnabled = Enabled;

    if (this->IsWatchdogEnabled)
    {
        timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_EN);
    }
    else
    {
        timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_DIS);
    }
}
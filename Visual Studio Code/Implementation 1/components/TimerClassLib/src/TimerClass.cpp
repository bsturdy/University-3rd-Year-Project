#include "TimerClass.h"

// Hardware Config Macros
#define CyclicTimerGroup        TIMER_GROUP_0 
#define CyclicTimerIndex        TIMER_0
#define WatchdogTimerGroup      TIMER_GROUP_1
#define WatchdogTimerIndex      TIMER_0
#define CyclicPeriodInUs        CONFIG_ESP_CYCLIC_TASK_PERIOD
#define WatchdogPeriodInUs      CONFIG_ESP_WATCHDOG_TASK_PERIOD
#define Prescaler               CONFIG_ESP_TIMER_PRESCALER
#define TAG                     "Timer Class"

#ifndef CONFIG_ESP_CYCLIC_TASK_PERIOD
#define CONFIG_ESP_CYCLIC_TASK_PERIOD 1000 // default example
#endif

#define TAG "TimerClass"

// Initialize the static ISR pointer
TimerClass* TimerClass::isr_instance = nullptr;





//==============================================================================//
//                                                                              //
//            Constructors, Destructors, Internal Functions                     //
//                                                                              //
//==============================================================================// 

TimerClass& TimerClass::GetInstance()
{
    static TimerClass instance;
    return instance;
}

TimerClass::TimerClass()
{
    //ISRs can find us without calling GetInstance()
    isr_instance = this;
    
    // Default initialization
    CycleTimeMs = CyclicPeriodInUs / 1000.0; 
    WatchdogTimeMs = WatchdogPeriodInUs / 1000.0;
    Prescalar = Prescaler;     // Default
}

TimerClass::~TimerClass()
{
    isr_instance = nullptr;
}



bool IRAM_ATTR TimerClass::CyclicISR(void* arg) 
{
    // Do not access if instance is gone
    if (isr_instance == nullptr) return false;

    // Access instance members via the pointer
    isr_instance->CyclicIsrCounter++;
    
    // Notify Task
    if (isr_instance->CyclicTaskHandle != NULL) {
        vTaskNotifyGiveFromISR(isr_instance->CyclicTaskHandle, &isr_instance->xHigherPriorityTaskWokenFalse);
    }

    portYIELD_FROM_ISR(isr_instance->xHigherPriorityTaskWokenFalse);  
    return true;
}

bool IRAM_ATTR TimerClass::WatchdogISR(void* arg) 
{ 
    if (isr_instance == nullptr) return false;

    isr_instance->WatchdogISRCounter++;

    if (isr_instance->WatchdogTaskHandle != NULL) {
        vTaskNotifyGiveFromISR(isr_instance->WatchdogTaskHandle, &isr_instance->xHigherPriorityTaskWokenTrue);
    }

    portYIELD_FROM_ISR(isr_instance->xHigherPriorityTaskWokenTrue); 
    return true;
}

void TimerClass::CyclicTask(void* pvParameters) 
{
    TimerClass* self = (TimerClass*)pvParameters;
    ESP_LOGI(TAG, "Cyclic Task Started on Core %d", xPortGetCoreID());

    while (true) 
    {
        // Block until notified by ISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // esp_rom_printf("Woke\n"); 

        if (self->AreTimersInitated && self->UserTask != nullptr)
        {
            // Reset and start watchdog
            timer_set_counter_value(WatchdogTimerGroup, WatchdogTimerIndex, 0);
            timer_start(WatchdogTimerGroup, WatchdogTimerIndex);
            
            // Run user task
            self->UserTask(NULL);
            self->CyclicTaskCounter++;

            // Pause watchdog
            timer_pause(WatchdogTimerGroup, WatchdogTimerIndex);
        }
    }
}

void TimerClass::WatchdogTask(void* pvParameters)
{
    TimerClass* self = (TimerClass*)pvParameters;

    while(true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (self->AreTimersInitated)
        {
            ESP_LOGE(TAG, "Watchdog Triggered! Resetting Cyclic Task.");

            timer_pause(WatchdogTimerGroup, WatchdogTimerIndex);

            // Re-create cyclic task
            if (self->CyclicTaskHandle != NULL) {
                vTaskDelete(self->CyclicTaskHandle);
                self->CyclicTaskHandle = NULL;
            }

            self->CyclicTaskHandle = xTaskCreateStaticPinnedToCore
            (
                CyclicTask,                
                "Deterministic Task",      
                CyclicTaskStackSize,                 
                self,                       // <--- Pass 'this' (self) here!
                configMAX_PRIORITIES - 2,  
                self->CyclicTaskStack,     // Use instance member     
                &self->CyclicTaskTCB,      // Use instance member       
                self->CoreToRunCyclicTask
            );

            timer_set_counter_value(WatchdogTimerGroup, WatchdogTimerIndex, 0);

            if (self->IsWatchdogEnabled)
            {
                timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_EN);
            }
            self->WatchdogTaskCounter++;
        }
    }
}





//==============================================================================// 
//                                                                              //
//                       Public Setup Functions                                 //
//                                                                              //
//==============================================================================// 

bool TimerClass::SetupCyclicTask(void (*TaskToRun)(void*), uint8_t CoreToUse)
{
    if (this->IsSetupDone) return true;

    ESP_LOGI(TAG, "Setting up Cyclic Task");

    this->CoreToRunCyclicTask = CoreToUse;
    this->UserTask = TaskToRun;

    // Create Tasks    
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

    // Call internal setup
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

    // Ensure task exists before starting timer
    if (this->CyclicTaskHandle == NULL) {
        ESP_LOGE(TAG, "CyclicTaskHandle is NULL. Call SetupCyclicTask first.");
        return false;
    }

    // Configure Cyclic Timer
    timer_config_t CyclicConfig = {
        .alarm_en = TIMER_ALARM_DIS,       // Alarm is enabled later
        .counter_en = TIMER_START,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .clk_src = TIMER_SRC_CLK_APB,
        .divider = (uint32_t)this->Prescalar // Cast to ensure correct type
    };

    ESP_ERROR_CHECK(timer_init(CyclicTimerGroup, CyclicTimerIndex, &CyclicConfig));
    
    // Calculate alarm value (ticks)
    uint64_t alarm_val = (80000000.0 / this->Prescalar) * (this->CycleTimeMs / 1000.0);
    ESP_ERROR_CHECK(timer_set_alarm_value(CyclicTimerGroup, CyclicTimerIndex, alarm_val));
    
    // Enable Interrupts
    ESP_ERROR_CHECK(timer_enable_intr(CyclicTimerGroup, CyclicTimerIndex));
    
    // Link ISR
    ESP_ERROR_CHECK(timer_isr_callback_add(CyclicTimerGroup, CyclicTimerIndex, CyclicISR, NULL, ESP_INTR_FLAG_IRAM));
    
    // Start Timer
    ESP_ERROR_CHECK(timer_start(CyclicTimerGroup, CyclicTimerIndex));
    
    // Enable Alarm Action
    ESP_ERROR_CHECK(timer_set_alarm(CyclicTimerGroup, CyclicTimerIndex, TIMER_ALARM_EN));

    // Configure Watchdog Timer
    timer_config_t WatchdogConfig = {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,         // Starts paused
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_DIS,
        .clk_src = TIMER_SRC_CLK_APB,
        .divider = (uint32_t)this->Prescalar
    };

    ESP_ERROR_CHECK(timer_init(WatchdogTimerGroup, WatchdogTimerIndex, &WatchdogConfig));
    
    uint64_t wd_alarm_val = (80000000.0 / this->Prescalar) * (this->WatchdogTimeMs / 1000.0);
    ESP_ERROR_CHECK(timer_set_alarm_value(WatchdogTimerGroup, WatchdogTimerIndex, wd_alarm_val));
    
    ESP_ERROR_CHECK(timer_enable_intr(WatchdogTimerGroup, WatchdogTimerIndex));
    ESP_ERROR_CHECK(timer_isr_callback_add(WatchdogTimerGroup, WatchdogTimerIndex, WatchdogISR, NULL, ESP_INTR_FLAG_IRAM));

    // Mark as ready so the task loop can proceed
    this->AreTimersInitated = true;

    ESP_LOGI(TAG, "SetupTimer Successful! Alarm Value: %llu", alarm_val);
    return true;
}

void TimerClass::SetWatchdogOnOff(bool Enabled)
{
    this->IsWatchdogEnabled = Enabled;
    if (this->IsWatchdogEnabled) {
        timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_EN);
    } else {
        timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_DIS);
    }
}
#include "TimerClass.h"



//=============================================================================// 
//                            Timer Class                                      //
//=============================================================================// 

#define CyclicTimerGroup        TIMER_GROUP_0
#define CyclicTimerIndex        TIMER_0
#define WatchdogTimerGroup      TIMER_GROUP_1
#define WatchdogTimerIndex      TIMER_0

StackType_t TimerClass::StaticTaskStack[StackSize];
StaticTask_t TimerClass::StaticTaskTCB;

TimerClass* ClassInstance;
TaskHandle_t CyclicTaskHandle = NULL;
TaskHandle_t WatchdogTaskHandle = NULL;
BaseType_t xHigherPriorityTaskWoken1;
BaseType_t xHigherPriorityTaskWoken2;
const char *TAG = "Timer Class";



//=============================================================================//
//               Constructors, Destructors, Internal Functions                 //
//=============================================================================// 

TimerClass::TimerClass()
{
    ClassInstance = this;
}

TimerClass::~TimerClass()
{
    ;
}

// Task called by cyclic ISR. This task calls the user task
void TimerClass::CyclicTask(void* pvParameters) 
{
    while (true) 
    {
        // Block until called
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (ClassInstance->AreTimersInitated)
        {

            // ======== START USER CODE ======== //

            if (ClassInstance->UserTask) 
            {
                ESP_ERROR_CHECK(timer_set_counter_value(WatchdogTimerGroup, WatchdogTimerIndex, 0));
                ESP_ERROR_CHECK(timer_start(WatchdogTimerGroup, WatchdogTimerIndex));

                ClassInstance->CyclicTaskCounter++;
                
                ClassInstance->UserTask(NULL);

                ESP_ERROR_CHECK(timer_pause(WatchdogTimerGroup, WatchdogTimerIndex));
            }

            // ======== END USER CODE ======== //

        }
    }
}

// Task called by Watchdog ISR. This task resets the cyclic task
void TimerClass::WatchdogTask(void* pvParameters)
{
    while(true)
    {
        // Block until called
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (ClassInstance->AreTimersInitated)
        {
            ClassInstance->WatchdogTaskCounter++;

            ESP_ERROR_CHECK(timer_pause(WatchdogTimerGroup, WatchdogTimerIndex));

            vTaskDelete(CyclicTaskHandle);

            CyclicTaskHandle = NULL;

            CyclicTaskHandle = xTaskCreateStatic
            (
                CyclicTask,                // Task function
                "Deterministic Task",      // Task name
                StackSize,                 // Stack depth
                NULL,                      // Parameters to pass
                configMAX_PRIORITIES - 2,  // Highest priority
                StaticTaskStack,           // Preallocated stack memory
                &StaticTaskTCB             // Preallocated TCB memory
            );

            ESP_ERROR_CHECK(timer_set_counter_value(WatchdogTimerGroup, WatchdogTimerIndex, 0));

            if (ClassInstance->IsWatchdogEnabled)
            {
                ESP_ERROR_CHECK(timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_EN));
            }

        }
    }
}

// ISR called cyclically by a hardware timer, signals deterministic task to run
bool IRAM_ATTR TimerClass::CyclicISR(void* arg) 
{
    ClassInstance->CyclicIsrCounter++;
    
    xHigherPriorityTaskWoken1 = pdFALSE;
    vTaskNotifyGiveFromISR(CyclicTaskHandle, &xHigherPriorityTaskWoken1);
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken1);  
    return true;
}

// ISR called by a timer triggered in the cyclic task, signals that a task has taken too long and must be terminated
bool IRAM_ATTR TimerClass::WatchdogISR(void* arg) 
{ 
    ClassInstance->WatchdogISRCounter++;

    xHigherPriorityTaskWoken2 = pdTRUE;
    vTaskNotifyGiveFromISR(WatchdogTaskHandle, &xHigherPriorityTaskWoken2);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken2); 
    return true;
}



//=============================================================================// 
//                           Setup Functions                                   //
//=============================================================================// 

// Configures cyclic timer at 80MHz to call cyclic ISR
void TimerClass::SetupTimer(float CycleTimeInMs, float WatchdogTime, uint16_t Prescalar)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupTimer Executed!");

    ClassInstance->CycleTimeMs = CycleTimeInMs;
    ClassInstance->MaxExecutionTimeMs = WatchdogTime;
    ClassInstance->Prescalar = Prescalar;

    // If cyclic task doesnt exist yet, wait
    while(CyclicTaskHandle == NULL)
    {
        vTaskDelay(100);
    }

    // Configure cyclic timer
    timer_config_t CyclicConfig =
    {
        .alarm_en = TIMER_ALARM_DIS,
        .counter_en = TIMER_START,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .clk_src = TIMER_SRC_CLK_PLL_F80M,
        .divider = ClassInstance->Prescalar,
        //.intr_type = 
    };

    // init timer
    timer_init(CyclicTimerGroup, CyclicTimerIndex, &CyclicConfig);
    // Set alarm up
    timer_set_alarm_value(CyclicTimerGroup, CyclicTimerIndex, ((80000000/ClassInstance->Prescalar) * (ClassInstance->CycleTimeMs/1000)));
    // Enable interrupt on timer
    timer_enable_intr(CyclicTimerGroup, CyclicTimerIndex);
    // Link ISR callback for timer
    timer_isr_callback_add(CyclicTimerGroup, CyclicTimerIndex, CyclicISR, NULL, 0);
    // Enable timer
    timer_start(CyclicTimerGroup, CyclicTimerIndex);
    // Enable alarm
    timer_set_alarm(CyclicTimerGroup, CyclicTimerIndex, TIMER_ALARM_EN);

    // Configure Watchdog timer
    timer_config_t WatchdogConfig = 
    {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_DIS,
        .clk_src = TIMER_SRC_CLK_PLL_F80M,
        .divider = ClassInstance->Prescalar,
        //.intr_type =        
    };

    // init timer
    timer_init(WatchdogTimerGroup, WatchdogTimerIndex, &WatchdogConfig);
    // Set alarm value
    timer_set_alarm_value(WatchdogTimerGroup, WatchdogTimerIndex, ((80000000/ClassInstance->Prescalar) * (ClassInstance->MaxExecutionTimeMs/1000)));
    // Enable interrupt on timer 
    timer_enable_intr(WatchdogTimerGroup, WatchdogTimerIndex);
    // Link ISR callback for timer
    timer_isr_callback_add(WatchdogTimerGroup, WatchdogTimerIndex, WatchdogISR, NULL, 0);

    AreTimersInitated = true;

    ESP_LOGI(TAG, "SetupTimer Successful!");
    printf("\n");
}   

// Takes user-input task and assigns it to UserTask pointer
void TimerClass::SetupCyclicTask(void (*TaskToRun)(void*), float CycleTimeInMs, float WatchdogTime, uint16_t Prescalar)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupCyclicTask Executed!");

    UserTask = TaskToRun;

    // Create the task in static location
    CyclicTaskHandle = xTaskCreateStatic
    (
        CyclicTask,         // Task function
        "Deterministic Task",      // Task name
        StackSize,                 // Stack depth
        NULL,                      // Parameters to pass
        configMAX_PRIORITIES - 2,  // Highest priority
        StaticTaskStack,           // Preallocated stack memory
        &StaticTaskTCB             // Preallocated TCB memory
    );

    xTaskCreate
    (
        WatchdogTask,
        "Watchdog Task",
        512,
        NULL,
        configMAX_PRIORITIES - 1,
        &WatchdogTaskHandle
    );

    // Check if the task was successfully created
    if (CyclicTaskHandle == NULL) 
    {
        ESP_LOGI(TAG, "Failed To Create CyclicTask");
    }
    if (WatchdogTaskHandle == NULL)
    {
        ESP_LOGI(TAG, "Failed To Create Watchdog Task");
    }

    ESP_LOGI(TAG, "SetupCyclicTask Successful!");
    printf("\n");

    ClassInstance->SetupTimer(CycleTimeInMs, WatchdogTime, Prescalar);
}



//=============================================================================// 
//                             Get / Set                                       //
//=============================================================================// 

uint64_t TimerClass::GetCyclicIsrCounter()
{
    return CyclicIsrCounter;
}

uint64_t TimerClass::GetCyclicTaskCounter()
{
    return CyclicTaskCounter;
}

uint64_t TimerClass::GetWatchdogIsrCounter()
{
    return WatchdogISRCounter;
}

uint64_t TimerClass::GetWatchdogTaskCounter()
{
    return WatchdogTaskCounter;
}

uint64_t TimerClass::GetTimerFrequency()
{
    return 80000000/Prescalar;
}

// Determines if the Watchdog functionality is used or not 
void TimerClass::SetWatchdogOnOff(bool IsWatchdogEnabled)
{
    ClassInstance->IsWatchdogEnabled = IsWatchdogEnabled;
    if (ClassInstance->IsWatchdogEnabled)
    {
        timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_EN);
    }
}
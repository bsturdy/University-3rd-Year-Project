#include "TimerClass.h"



//=============================================================================// 
//                            Timer Class                                      //
//=============================================================================// 

#define CyclicTimerGroup        ((timer_group_t)(CONFIG_ESP_CYCLIC_TIMER_GROUP == 0 ? TIMER_GROUP_0 : TIMER_GROUP_1))
#define CyclicTimerIndex        ((timer_idx_t)(CONFIG_ESP_CYCLIC_TIMER_INDEX == 0 ? TIMER_0 : TIMER_1))
#define WatchdogTimerGroup      ((timer_group_t)(CONFIG_ESP_WATCHDOG_TIMER_GROUP == 0 ? TIMER_GROUP_0 : TIMER_GROUP_1))
#define WatchdogTimerIndex      ((timer_idx_t)(CONFIG_ESP_WATCHDOG_TIMER_INDEX == 0 ? TIMER_0 : TIMER_1))
#define CyclicPeriodInUs        CONFIG_ESP_CYCLIC_TASK_PERIOD
#define WatchdogPeriodInUs      CONFIG_ESP_WATCHDOG_TIMEOUT
#define TAG                     "Timer Class"

static TimerClass* ClassInstance;

StackType_t TimerClass::CyclicTaskStack[CyclicTaskStackSize];
StaticTask_t TimerClass::CyclicTaskTCB;

StackType_t TimerClass::WatchdogTaskStack[WatchdogTaskStackSize];
StaticTask_t TimerClass::WatchdogTaskTCB;

TaskHandle_t CyclicTaskHandle = NULL;
TaskHandle_t WatchdogTaskHandle = NULL;

BaseType_t xHigherPriorityTaskWoken1;
BaseType_t xHigherPriorityTaskWoken2;





//=============================================================================//
//            Constructors, Destructors, Internal Functions                    //
//=============================================================================// 

// Constructor
TimerClass::TimerClass()
{
    // Pointer to itself
    ClassInstance = this;
}

// Destructor (Unsused)
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

            if (ClassInstance->UserTask == nullptr)
            {
                return;
            }

            // ======== START USER CODE ======== //

            if (ClassInstance->UserTask) 
            {
                // Reset and start watchdog timer
                ESP_ERROR_CHECK(timer_set_counter_value(WatchdogTimerGroup, WatchdogTimerIndex, 0));
                ESP_ERROR_CHECK(timer_start(WatchdogTimerGroup, WatchdogTimerIndex));
                
                // Run user task
                ClassInstance->UserTask(NULL);

                // Increment task counter
                ClassInstance->CyclicTaskCounter++;

                // Pause watchdog timer
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
            // Pause watchdog timer
            ESP_ERROR_CHECK(timer_pause(WatchdogTimerGroup, WatchdogTimerIndex));

            // Delete cyclic task
            vTaskDelete(CyclicTaskHandle);
            CyclicTaskHandle = NULL;

            // Re-create cyclic task (static memory location for fast speeds)
            CyclicTaskHandle = xTaskCreateStaticPinnedToCore
            (
                CyclicTask,                // Task function
                "Deterministic Task",      // Task name
                CyclicTaskStackSize,                 // Stack depth
                NULL,                      // Parameters to pass
                configMAX_PRIORITIES - 2,  // Highest priority
                CyclicTaskStack,           // Preallocated stack memory
                &CyclicTaskTCB,             // Preallocated TCB memory
                ClassInstance->CoreToRunCyclicTask
            );

            // Reset watchdog timer
            ESP_ERROR_CHECK(timer_set_counter_value(WatchdogTimerGroup, WatchdogTimerIndex, 0));

            // Enable timers alarm if optioned
            if (ClassInstance->IsWatchdogEnabled)
            {
                ESP_ERROR_CHECK(timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_EN));
            }

            // Increment task counter
            ClassInstance->WatchdogTaskCounter++;

        }
    }
}

// ISR called cyclically by a hardware timer, signals deterministic task to run
bool IRAM_ATTR TimerClass::CyclicISR(void* arg) 
{
    // Increment ISR counter
    ClassInstance->CyclicIsrCounter++;
    
    // Notify task to run with appropriate priority interruption
    xHigherPriorityTaskWoken1 = pdFALSE;
    vTaskNotifyGiveFromISR(CyclicTaskHandle, &xHigherPriorityTaskWoken1);

    // End
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken1);  
    return true;
}

// ISR called by a timer triggered in the cyclic task, signals that a task has taken too long and must be terminated
bool IRAM_ATTR TimerClass::WatchdogISR(void* arg) 
{ 
    // Increment ISR counter
    ClassInstance->WatchdogISRCounter++;

    // Notify task to run with appropriate priority interruption
    xHigherPriorityTaskWoken2 = pdTRUE;
    vTaskNotifyGiveFromISR(WatchdogTaskHandle, &xHigherPriorityTaskWoken2);

    // End
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken2); 
    return true;
}



//=============================================================================// 
//                           Setup Functions                                   //
//=============================================================================// 

// Configures cyclic timer at 80MHz to call cyclic ISR.
bool TimerClass::SetupTimer(float CycleTimeInMs, float WatchdogTime, uint16_t Prescalar)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupTimer Executed!");

    // Assign vars
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
        .clk_src = TIMER_SRC_CLK_APB,
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
        .clk_src = TIMER_SRC_CLK_APB,
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

    return true;
}   

// Takes a task input and converts it into a cyclically executed task with watchdog protection
bool TimerClass::SetupCyclicTask(void (*TaskToRun)(void*), /*float CycleTimeInUs, float WatchdogTimeInUs,*/ 
                                 uint16_t Prescalar, uint8_t CoreToUse)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupCyclicTask Executed!");

    float CycleTimeInMs = CyclicPeriodInUs / 1000.0;
    float WatchdogTimeInMs = WatchdogPeriodInUs / 1000.0;

    // Assign vars
    CoreToRunCyclicTask = CoreToUse;
    UserTask = TaskToRun;

    // Create the cyclic task in static location (fast speeds for deleting and recreating)
    CyclicTaskHandle = xTaskCreateStaticPinnedToCore
    (
        CyclicTask,                     // Task function
        "Deterministic Task",           // Task name
        CyclicTaskStackSize,            // Stack depth
        NULL,                           // Parameters to pass
        configMAX_PRIORITIES - 2,       // Highest priority - 1
        CyclicTaskStack,                // Preallocated stack memory
        &CyclicTaskTCB,                 // Preallocated TCB memory
        CoreToRunCyclicTask             // Core task is pinned to
    );

    // Create the watchdog task in static location (fast speeds for deleting and recreating)
    WatchdogTaskHandle = xTaskCreateStaticPinnedToCore
    (
        WatchdogTask,                   // Task function
        "Watchdog Task",                // Task name
        WatchdogTaskStackSize,          // Stack depth
        NULL,                           // Parameters to pass
        configMAX_PRIORITIES - 1,       // Highest priority - 1
        WatchdogTaskStack,              // Preallocated stack memory
        &WatchdogTaskTCB,               // Preallocated TCB memory
        CoreToRunCyclicTask             // Core task is pinned to
    );   


    // Check if the tasks were successfully created
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

    // Initiate the timers after the tasks are created
    return ClassInstance->SetupTimer(CycleTimeInMs, WatchdogTimeInMs, Prescalar);
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

// Determines if the Watchdog functionality is used or not.
void TimerClass::SetWatchdogOnOff(bool IsWatchdogEnabled)
{
    ClassInstance->IsWatchdogEnabled = IsWatchdogEnabled;
    if (ClassInstance->IsWatchdogEnabled)
    {
        timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_EN);
    }
}
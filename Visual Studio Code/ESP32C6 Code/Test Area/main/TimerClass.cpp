#include "TimerClass.h"



//=============================================================//
//    Timer Class                                              //
//=============================================================// 

#define CyclicTimerGroup        TIMER_GROUP_0
#define CyclicTimerIndex        TIMER_0
#define WatchdogTimerGroup      TIMER_GROUP_1
#define WatchdogTimerIndex      TIMER_0

const int TimerClass::StackSize;
StackType_t TimerClass::StaticTaskStack[StackSize];
StaticTask_t TimerClass::StaticTaskTCB;

TimerClass* ClassInstance;
static TaskHandle_t DeterministicTaskHandle = NULL;
static TaskHandle_t WatchdogTaskHandle = NULL;
static BaseType_t xHigherPriorityTaskWoken1;
static BaseType_t xHigherPriorityTaskWoken2;
static const char *TAG = "Timer Class";



//=============================================================//
//    Constructors, Destructors, Internal Functions            //
//=============================================================// 

TimerClass::TimerClass()
{
    ClassInstance = this;
}

TimerClass::~TimerClass()
{
    ;
}

void TimerClass::CyclicTask(void* pvParameters) 
{
    while (true) 
    {
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

void TimerClass::WatchdogTask(void* pvParameters)
{
    while(true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (ClassInstance->AreTimersInitated)
        {
            ClassInstance->WatchdogTaskCounter++;

            timer_pause(WatchdogTimerGroup, WatchdogTimerIndex);

            vTaskSuspend(DeterministicTaskHandle);

            if (DeterministicTaskHandle != NULL) 
            {
                vTaskDelete(DeterministicTaskHandle);
                DeterministicTaskHandle = NULL; // Clear the handle
            }

            DeterministicTaskHandle = xTaskCreateStatic
            (
                CyclicTask,                // Task function
                "Deterministic Task",      // Task name
                StackSize,                 // Stack depth
                NULL,                      // Parameters to pass
                configMAX_PRIORITIES - 2,  // Highest priority
                StaticTaskStack,           // Preallocated stack memory
                &StaticTaskTCB             // Preallocated TCB memory
            );

            timer_set_counter_value(WatchdogTimerGroup, WatchdogTimerIndex, 0);

            if (ClassInstance->IsWatchdogEnabled)
            {
                timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_EN);
            }
            
        }
    }
}

bool IRAM_ATTR TimerClass::CyclicISR(void* arg) 
{
    ClassInstance->CyclicIsrCounter++;
    
    xHigherPriorityTaskWoken1 = pdFALSE;
    vTaskNotifyGiveFromISR(DeterministicTaskHandle, &xHigherPriorityTaskWoken1);
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken1);  
    return true;
}

bool IRAM_ATTR TimerClass::WatchdogISR(void* arg) 
{ 
    ClassInstance->WatchdogISRCounter++;

    xHigherPriorityTaskWoken2 = pdTRUE;
    vTaskNotifyGiveFromISR(WatchdogTaskHandle, &xHigherPriorityTaskWoken2);

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken2); 
    return true;
}



//=============================================================//
//    Setup Functions                                          //
//=============================================================// 

void TimerClass::SetupTimer(float CycleTimeInMs, float WatchdogTime, uint16_t Prescalar)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupTimer Executed!");

    ClassInstance->CycleTimeMs = CycleTimeInMs; /// 5;
    ClassInstance->MaxExecutionTimeMs = WatchdogTime;
    ClassInstance->Prescalar = Prescalar;

    // If no task to link with ISR to, block process
    while(DeterministicTaskHandle == NULL)
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


    // Configure timeout timer
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

void TimerClass::SetupDeterministicTask(void (*TaskToRun)(void*))
{
    printf("\n");
    ESP_LOGI(TAG, "SetupDeterministicTask Executed!");

    UserTask = TaskToRun;


    // Create the task using static memory
    DeterministicTaskHandle = xTaskCreateStatic
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
    if (DeterministicTaskHandle == NULL) 
    {
        ESP_LOGI(TAG, "Fucked it Det");
    }
    if (WatchdogTaskHandle == NULL)
    {
        ESP_LOGI(TAG, "Fucked it wdog");
    }

    ESP_LOGI(TAG, "SetupDeterministicTask Successful!");
    printf("\n");
}



//=============================================================//
//    Get / Set                                                //
//=============================================================// 

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

void TimerClass::SetWatchdogOnOff(bool IsWatchdogEnabled)
{
    ClassInstance->IsWatchdogEnabled = IsWatchdogEnabled;
    if (ClassInstance->IsWatchdogEnabled)
    {
        timer_set_alarm(WatchdogTimerGroup, WatchdogTimerIndex, TIMER_ALARM_EN);
    }
}
#include "TimerClass.h"



//=============================================================//
//    Timer Class                                              //
//=============================================================// 

#define CyclicTimerGroup    TIMER_GROUP_0
#define CyclicTimerIndex    TIMER_0
#define TimeoutTimerGroup   TIMER_GROUP_1
#define TimeoutTimerIndex   TIMER_0

TimerClass* ClassInstance;
static TaskHandle_t DeterministicTaskHandle = NULL;
static BaseType_t xHigherPriorityTaskWoken;
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

void TimerClass::DeterministicTask(void *pvParameters) 
{
    while (true) 
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ClassInstance->TaskCounter++;
        // ======== START USER CODE ======== //

        if (ClassInstance->UserTask) 
        {
            ClassInstance->UserTask(NULL); // Call the user-defined task directly
        }
        
        // ======== END USER CODE ======== //
    }
}

bool IRAM_ATTR TimerClass::CyclicTimerISR(void* arg) 
{
    // Timer Interrupt Logic (Cyclic Timer)
    ClassInstance->IsrCounter++;
    
    // Notify task (deterministic task)
    xHigherPriorityTaskWoken = pdTRUE;

    vTaskNotifyGiveFromISR(DeterministicTaskHandle, &xHigherPriorityTaskWoken);
    
    portYIELD_FROM_ISR();  
    
    return true;
}

bool IRAM_ATTR TimerClass::TimeoutTimerISR(void* arg) 
{
    portYIELD_FROM_ISR(); 
    return true;
}



//=============================================================//
//    Setup Functions                                          //
//=============================================================// 

bool TimerClass::SetupTimer(float CycleTimeInMs, uint16_t Prescalar)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupTimer Executed!");

    ClassInstance->CycleTimeMs = CycleTimeInMs;
    ClassInstance->Prescalar = Prescalar;

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

    // Start timer
    timer_init(CyclicTimerGroup, CyclicTimerIndex, &CyclicConfig);
    // Set alarm up
    timer_set_alarm_value(CyclicTimerGroup, CyclicTimerIndex, ((80000000/ClassInstance->Prescalar) * (ClassInstance->CycleTimeMs/1000)));
    // Enable interrupt on timer
    timer_enable_intr(CyclicTimerGroup, CyclicTimerIndex);
    // Link ISR callback for timer
    timer_isr_callback_add(CyclicTimerGroup, CyclicTimerIndex, CyclicTimerISR, NULL, 0);
    // Enable timer
    timer_start(CyclicTimerGroup, CyclicTimerIndex);
    // Enable alarm
    timer_set_alarm(CyclicTimerGroup, CyclicTimerIndex, TIMER_ALARM_EN);


    /*// Configure timeout timer
    timer_config_t TimeoutConfig = 
    {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_DIS,
        .clk_src = TIMER_SRC_CLK_PLL_F80M,
        .divider = ClassInstance->Prescalar,
        //.intr_type =        
    };

    // Start timer
    timer_init(TimeoutTimerGroup, TimeoutTimerIndex, &TimeoutConfig);
    // Set alarm value
    timer_set_alarm_value(TimeoutTimerGroup, TimeoutTimerIndex, (ClassInstance->MaxExecutionTimeMs * 1000));
    // Enable interrupt on timer 
    timer_enable_intr(TimeoutTimerGroup, TimeoutTimerIndex);
    // Link ISR callback for timer
    timer_isr_callback_add(TimeoutTimerGroup, TimeoutTimerIndex, TimeoutTimerISR, NULL, 0);*/

    ESP_LOGI(TAG, "SetupTimer Successful!");
    printf("\n");

    return true;
}   

void TimerClass::SetupDeterministicTask(void (*TaskToRun)(void*), uint32_t StackDepth)
{
    UserTask = TaskToRun;

    xTaskCreate(DeterministicTask, "Deterministic Task", 2048, NULL, configMAX_PRIORITIES - 1, &DeterministicTaskHandle);
}



//=============================================================//
//    Get / Set                                                //
//=============================================================// 

uint64_t TimerClass::GetIsrCounter()
{
    return IsrCounter;
}

uint64_t TimerClass::GetTaskCounter()
{
    return TaskCounter;
}

uint64_t TimerClass::GetTimerFrequency()
{
    return 80000000/Prescalar;
}

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "GpioClass.h"
#include "TimerClass.h"
#include "WifiClass.h"
#include "UtilitiesClass.h"
#include "packet_processors.h"
#include "tests.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#define TAG "Main"
#define CYCLIC_TAG "CyclicTask"

// --- UI Helpers ---
#define BOLD   "\x1b[1m"
#define RESET  "\x1b[0m"
#define GREEN  "\x1b[32m"
#define CYAN   "\x1b[36m"
#define YELLOW "\x1b[33m"

Station* WifiSta = nullptr;
AccessPointStation* WifiApSta = nullptr;
Packet1 packet1;
ITF_PacketProcessor* processor = &packet1;

uint64_t Uid = CONFIG_ESP_NODE_UID;
uint8_t MainState = 0;
uint64_t CyclicCalls = 0;
uint8_t CyclicState = 0;
uint8_t TestFails = 0;


void CyclicTask1(void* pvParameters)
{
    CyclicCalls++;

    if (!WifiApSta->IsConnectedToHost()) CyclicState = 99;

    switch(CyclicState)
    {
        case 0:
            break;


        default:
            CyclicState = 0;
            break;
    }
};



extern "C" void app_main(void)
{

    // Init singleton instances
    //WifiSta = WifiFactory::CreateStation(1, 10050, true);
    WifiApSta = WifiFactory::CreateAccessPointStation(1, 10050, true);
    TimerClass::GetInstance();
    GpioClass::GetInstance();
    UtilitiesClass::GetInstance();


    // Main event loop
    while(true)
    {
        switch(MainState)
        {
            case 0: // Power on
                ESP_LOGI(TAG, "POWER ON");
                if (GpioClass::GetInstance().SetupOnboardLed()) MainState = 1;
                break;
            
            
            case 1: // Run tests
                TestFails = RunAllTests();
                if (TestFails == 0) MainState = 2;
                else MainState = 99;
                break;


            case 2: // Start cyclic task
                if (TimerClass::GetInstance().SetupCyclicTask(CyclicTask1, 0)) MainState = 3;
                else MainState = 99;
                break;


            case 3: // Connect to wifi
                GpioClass::GetInstance().ChangeOnboardLedColour(0, 0, 255);
                WifiApSta->SetupWifi();
                if (WifiApSta->IsConnectedToHost()) MainState = 4;
                break;


            case 4: // Normal operation
                GpioClass::GetInstance().ChangeOnboardLedColour(0, 255, 0);
                if (not WifiApSta->IsConnectedToHost()) MainState = 3;
                else
                {
                    float temp = UtilitiesClass::GetInstance().GetChipTemperatureC();
                    float rssi = UtilitiesClass::GetInstance().GetWifiSignalStrength();
                    size_t heap = UtilitiesClass::GetInstance().GetFreeHeapBytes();
                    uint64_t uptime = UtilitiesClass::GetInstance().GetUptimeMs();

                    //printf("\033[H\033[J"); // Clears terminal so the dashboard stays at the top
                    printf(BOLD GREEN "┌────────────────────────────────────────────────────────────┐" RESET "\n");
                    printf(BOLD GREEN "│" RESET BOLD "                   ESP32 S3 NODE DASHBOARD                  " BOLD GREEN "│" RESET "\n");
                    printf(BOLD GREEN "├──────────────────────────────┬─────────────────────────────┤" RESET "\n");

                    printf(BOLD GREEN "│" RESET "  " BOLD "SYSTEM METRICS" RESET "              " BOLD GREEN "│" RESET "  " BOLD "NETWORK STATUS" RESET "             " BOLD GREEN "│" RESET "\n");
                    printf(BOLD GREEN "│" RESET "  Uptime: " CYAN "%8llu ms" RESET "         " BOLD GREEN "│" RESET "  RSSI:     " YELLOW "%7.2f dBm" RESET "      " BOLD GREEN "│" RESET "\n", uptime, rssi);
                    printf(BOLD GREEN "│" RESET "  Heap:   " CYAN "%8zu B " RESET "         " BOLD GREEN "│" RESET "  My IP:  " GREEN "%15s" RESET "    " BOLD GREEN "│" RESET "\n", heap, WifiApSta->GetMyIpAddress());
                    printf(BOLD GREEN "│" RESET "  Temp:   " CYAN "%8.2f C " RESET "         " BOLD GREEN "│" RESET "  GW IP: " GREEN "%15s" RESET "     " BOLD GREEN "│" RESET "\n", temp, WifiApSta->GetParentIpAddress());

                    printf(BOLD GREEN "├──────────────────────────────┴─────────────────────────────┤" RESET "\n");
                    printf(BOLD GREEN "│" RESET "  " BOLD "TASK EXECUTION" RESET "                                            " BOLD GREEN "│" RESET "\n");
                    printf(BOLD GREEN "│" RESET "  Cyclic Calls: " YELLOW "%-10llu" RESET "                                  " BOLD GREEN "│" RESET "\n", CyclicCalls);
                    printf(BOLD GREEN "│" RESET "  Cyclic State: " YELLOW "%-5i" RESET "                                       " BOLD GREEN "│" RESET "\n", CyclicState);
                    printf(BOLD GREEN "└────────────────────────────────────────────────────────────┘" RESET "\n");
                    vTaskDelay(pdMS_TO_TICKS(900));
                }
                break;


            case 99: // Error state
                GpioClass::GetInstance().ChangeOnboardLedColour(255, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(500));
                GpioClass::GetInstance().ChangeOnboardLedColour(0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(400));
                printf("Critical error occurred! Test fails: %i\n", TestFails);
                break;


            default:
                break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

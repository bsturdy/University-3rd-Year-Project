#ifndef WifiClass_H
#define WifiClass_H

#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h" 
#include "esp_now.h"
#include <string.h>
#include <vector>
#include <algorithm>
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

struct ClientDevice
{
    uint64_t TimeOfConnection;
    uint8_t MacId[6];
    bool IsRegisteredWithEspNow;
};

class WifiClass
{
    private:
        static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);

        std::vector<ClientDevice> DeviceList;
        
        QueueHandle_t DeviceQueue;
        static void EspNowTask(void *pvParameters);
        static const int EspNowStackSize = 2048;                    
        static StackType_t EspNowTaskStack[EspNowStackSize];       
        static StaticTask_t EspNowTaskTCB;  
        bool EspNowRegisterDevice(ClientDevice* DeviceToRegister);
        bool EspNowDeleteDevice(ClientDevice* DeviceToDelete); 

    public:
        WifiClass();
        ~WifiClass();

        bool SetupWifiAP();
        bool SetupWifiClient();   
        bool SetupEspNow(); 

        size_t GetNumClientsConnected();
        bool GetIsConnectedToHost();
};

#endif
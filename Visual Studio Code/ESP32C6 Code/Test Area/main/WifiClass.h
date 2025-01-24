#ifndef WifiClass_H
#define WifiClass_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h" 
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
};

class WifiClass
{
    private:
        static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);

        std::vector<ClientDevice> DeviceList;

    public:
        WifiClass();
        ~WifiClass();

        // AP Functions
        bool SetupWifiAP();
        int GetNumClientsConnected();

        // Client Functions
        bool SetupWifiClient();
        bool GetIsConnectedToHost();
};

#endif
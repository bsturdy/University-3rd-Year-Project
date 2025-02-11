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
#include <sys/socket.h>


struct ClientDevice
{
    uint64_t TimeOfConnection;
    uint8_t MacId[6];
    bool IsRegisteredWithEspNow;
};

class WifiClass
{
    private:
        bool IsAp = false;
        bool IsSta = false;
        bool IsConnectedToAP;
        std::vector<ClientDevice> DeviceList;

        static void WifiEventHandlerAp(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);

        static void WifiEventHandlerSta(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);
        
        TaskHandle_t EspNowTaskHandle = NULL;
        TaskHandle_t UdpPollingTaskHandle = NULL;
        TaskHandle_t UdpProcessingTaskHandle = NULL;
        static void EspNowTask(void *pvParameters);
        static void UdpPollingTask (void *pvParameters);
        static void UdpProcessingTask(void* pvParameters);   

        static const int EspNowTaskStackSize = 4096;                    
        static StackType_t EspNowTaskStack[EspNowTaskStackSize];       
        static StaticTask_t EspNowTaskTCB;  

        static const int UdpPollingTaskStackSize = 2048;                  
        static StackType_t UdpPollingTaskStack[UdpPollingTaskStackSize];    
        static StaticTask_t UdpPollingTaskTCB;

        static const int UdpProcessingTaskStackSize = 4096;                  
        static StackType_t UdpProcessingTaskStack[UdpProcessingTaskStackSize];    
        static StaticTask_t UdpProcessingTaskTCB;

        QueueHandle_t EspNowDeviceQueue;
        QueueHandle_t UdpProcessingQueue;

        bool EspNowRegisterDevice(ClientDevice* DeviceToRegister);
        bool EspNowDeleteDevice(ClientDevice* DeviceToDelete); 
        bool SetupUdpSocket(uint16_t UDP_PORT);

        struct sockaddr_in UdpServerAddress;
        socklen_t UdpAddressLength = sizeof(UdpServerAddress);
        char UdpBuffer[1024];
        int UdpSocketFD;



    public:
        WifiClass();
        ~WifiClass();

        bool SetupWifiAP(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse);
        bool SetupWifiSta(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse);   
        bool SetupEspNow(uint8_t CoreToUse); 

        bool SendUdpPacket(const char* data, const char* dest_ip, uint16_t dest_port);

        size_t GetNumClientsConnected();
        bool GetIsConnectedToHost();
        bool GetIsAp();
        bool GetIsSta();
        TaskHandle_t GetEspNowTaskHandle();
        TaskHandle_t GetUdpPollingTaskHandle();
        TaskHandle_t GetUdpProcessingTaskHandle();
};

#endif
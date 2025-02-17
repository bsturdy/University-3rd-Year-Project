#ifndef WifiClass_H
#define WifiClass_H

// Author - Ben Sturdy
// This file implements a class 'Wifi Class'. This class should be instantiated
// only once in a project. This class controls all wireless functionalities.
// This class can set up a system as an Access Point or a Station in WiFi mode. 
// This class can set up and utilise ESP-NOW. The functions with this class can 
// run on the same core as other processes.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
        static void WifiEventHandlerAp(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);                // Handles all of the wifi events when the system is an AP

        static void WifiEventHandlerSta(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);                // Handles all of the wifi events when the system is a Station

        static void IpEventHandlerSta(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);                // Handles all of the IP events when the system is a Station
        
        static void EspNowTask(void *pvParameters);                                     // Task that handles the registering and de-registering of devices to ESP-NOW
        static void UdpPollingTask (void *pvParameters);                                // Task that listens for UDP packets to be received into the system
        static void UdpProcessingTask(void* pvParameters);                              // Task that processes received UDP packets appropriately
        static void UdpSystemTask(void* pvParameters);                                  // Task that manages UDP connection

        static const int EspNowTaskStackSize = 4096;                                    // Size given to ESP NOW task
        static StackType_t EspNowTaskStack[EspNowTaskStackSize];                        // Static memory size for ESP NOW task
        static StaticTask_t EspNowTaskTCB;                                              // Static memory location for ESP NOW task

        static const int UdpPollingTaskStackSize = 2048;                                // Size given to UDP Polling task    
        static StackType_t UdpPollingTaskStack[UdpPollingTaskStackSize];                // Static memory size for UDP Polling task
        static StaticTask_t UdpPollingTaskTCB;                                          // Static memory location for UDP Polling task

        static const int UdpProcessingTaskStackSize = 4096;                             // Size given to UDP Processing task    
        static StackType_t UdpProcessingTaskStack[UdpProcessingTaskStackSize];          // Static memory size for UDP Processing task
        static StaticTask_t UdpProcessingTaskTCB;                                       // Static memory location for UDP Processing task

        static const uint8_t UdpBufferSize = 1024;                                      // Size of UDP buffer

        TaskHandle_t EspNowTaskHandle = NULL;                                           // Task handle
        TaskHandle_t UdpPollingTaskHandle = NULL;                                       // Task handle
        TaskHandle_t UdpProcessingTaskHandle = NULL;                                    // Task handle
        QueueHandle_t EspNowDeviceQueue;                                                // Queue handle
        QueueHandle_t UdpProcessingQueue;                                               // Queue handle

        bool EspNowRegisterDevice(ClientDevice* DeviceToRegister);                      // Function to register a device to ESP NOW
        bool EspNowDeleteDevice(ClientDevice* DeviceToDelete);                          // Function to deregister / delete a device from ESP NOW
        bool SetupUdpSocket(uint16_t UDP_PORT);                                         // Function to establish a UDP socket on a given port

        bool IsAp = false;                                                              // Internal check
        bool IsSta = false;                                                             // Internal check
        bool IsConnectedToAP;                                                           // Is system connected to an Access Point (used when in station mode)
        std::vector<ClientDevice> DeviceList;                                           // List of devices connected to Access Point (used when in AP mode)
        char AccessPointIp[16];                                                         // IP address of Access Point connected to (used when in station mode)
        const uint16_t UdpPollingTaskCycleTime = 10;                                    // Cycle time of UDP polling task
        struct sockaddr_in UdpServerAddress;                                            // Address of UDP server
        socklen_t UdpAddressLength = sizeof(UdpServerAddress);                          // Length of UDP server address
        char UdpBuffer[UdpBufferSize];                                                  // Buffer for UDP data
        int UdpSocketFD;                                                                // File Descriptor of UDP socket



    protected:



    public:
        WifiClass();
        ~WifiClass();

        bool SetupWifiAP(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse);        // Sets up system as an Access Point. Parameters are configured through the Menu Config
        bool SetupWifiSta(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse);       // Sets up system as a station and polls for available Access Points. Parameters are configured through the Menu Config
        bool SetupEspNow(uint8_t CoreToUse);                                            // Sets up ESP-NOW configuration and detection

        bool SendUdpPacket(const char* data, const char* dest_ip, uint16_t dest_port);  // Sends a UDP packet to a given IP address and Port

        size_t GetNumClientsConnected();
        bool GetIsConnectedToHost();
        bool GetIsAp();
        bool GetIsSta();
        const char* GetApIpAddress();
        TaskHandle_t GetEspNowTaskHandle();
        TaskHandle_t GetUdpPollingTaskHandle();
        TaskHandle_t GetUdpProcessingTaskHandle();
};

#endif
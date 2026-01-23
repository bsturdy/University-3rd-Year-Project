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
#include <cstdint>
#include <string.h>
#include <vector>
#include <algorithm>
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static constexpr size_t UDP_SLOTS = 10;
static constexpr size_t UDP_SLOT_SIZE = 256;

struct UdpSlot 
{
    uint16_t len;
    uint8_t  data [UDP_SLOT_SIZE];
};

struct WifiDevice
{
    uint64_t TimeOfConnection;
    uint8_t MacId[6];
    uint16_t aid;
    char IpAddress[16];
};

struct UdpPacket
{
    int PacketLength = 0;
    char Data[128];
    char SenderIp[16];
    uint16_t SenderPort = 0;
};



class WifiClass
{
    private:
        static void WifiEventHandlerAp(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);                // Handles all of the wifi events when the system is an AP

        static void WifiEventHandlerSta(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);                // Handles all of the wifi events when the system is a Station
 
        static void IpEventHandlerAp(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);                // Handles all of the IP events when the system is an AP
        
        static void IpEventHandlerSta(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);                // Handles all of the IP events when the system is a Station
        


        portMUX_TYPE SlotMux = portMUX_INITIALIZER_UNLOCKED;
        uint8_t SlotHead  = 0;                                                          // next write index
        uint8_t SlotCount = 0;                                                          // number of valid slots (0..UDP_SLOTS)

        static void UdpRxTask(void* pvParameters);                                      // Task that receives UDP packets from the socket

        UdpSlot BufferSlots[UDP_SLOTS];                                                 // Size of UDP buffer

        TaskHandle_t UdpRxTaskHandle = NULL;                                            // Task handle
       
        bool SetupWifiAP(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse);        // Sets up system as an Access Point. Parameters are configured through the Menu Config
        bool SetupWifiSta(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse);       // Sets up system as a station and polls for available Access Points. Parameters are configured through the Menu Config

        bool StartUdp(uint16_t Port, uint8_t Core);

        esp_netif_t* ApNetif = nullptr;
        esp_netif_t* StaNetif = nullptr;

        int UdpSocket = -1;

        bool NvsReady = false;
        bool NetifReady = false;
        bool EventLoopReady = false;
        bool StationInterfaceReady = false;
        bool ApInterfaceReady = false;
        bool WifiStackReady = false;
        bool CountrySet = false;
        bool ApEventHandlersRegistered = false;
        bool StaEventHandlersRegistered = false;
        bool SetWifiModeDone = false;
        bool ApplyConfigDone = false;
        bool StartWifiDone = false;
        bool StaIpAcquired = false;
        bool UdpStarted = false;
        bool UdpTasksCreated = false;

        bool IsRuntimeLoggingEnabled = false;                                           // Print out logs for runtime functions
        bool IsAp = false;                                                              // Internal check
        bool IsSta = false;                                                             // Internal check
        bool IsConnectedToAP;                                                           // Is system connected to an Access Point (used when in station mode)
        WifiDevice HostWifiDevice;                                                      // AccessPoint that station is connected to (used when in station mode)
        //std::vector<WifiDevice> ClientWifiDeviceList;                                   // List of devices connected to Access Point (used when in AP mode)
        const uint16_t UdpPollingTaskCycleTime = 10;                                    // Cycle time of UDP polling task
        const uint16_t UdpSystemTaskCycleTime = 10;                                     // Cycle time of UDP system task
        struct sockaddr_in UdpHostServerAddress;                                        // Address of UDP server
        socklen_t UdpAddressLength = sizeof(UdpHostServerAddress);                      // Length of UDP server address
        int UdpSocketFD;                                                                // File Descriptor of UDP socket



    protected:



    public:
        WifiClass();
        ~WifiClass();

        bool SetupWifi(uint8_t CoreToUse);                                              // Sets up system according to Menu Configuration
        bool SetupEspNow(uint8_t CoreToUse);                                            // Sets up ESP-NOW configuration and detection
        bool SendUdpPacket(const char* data, const char* dest_ip, uint16_t dest_port);  // Sends a UDP packet to a given IP address and Port

        size_t GetNumClientsConnected();
        bool GetIsConnectedToHost();
        bool GetIsAp();
        bool GetIsSta();
        const char* GetApIpAddress();
        size_t GetDataFromBuffer(uint8_t* DataOut, bool* DataAvailable);
        TaskHandle_t GetEspNowTaskHandle();
        TaskHandle_t GetUdpPollingTaskHandle();
        TaskHandle_t GetUdpProcessingTaskHandle();

        void SetRuntimeLogging(bool EnableRuntimeLogging);                           // Produces logs for runtime functions. Useful for debugging
};



class Station // Singleton
{
    private:

        friend class WifiFactory;

        Station(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging);
        ~Station();

        //static Station* StaClassInstance;

        static void WifiEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);
        
        static void IpEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);
        

        // UDP Buffer management
        portMUX_TYPE SlotMux = portMUX_INITIALIZER_UNLOCKED;
        uint8_t SlotHead  = 0; 
        uint8_t SlotCount = 0; 

        static void UdpRxTask(void* pvParameters); 
        UdpSlot BufferSlots[UDP_SLOTS]{};

        TaskHandle_t UdpRxTaskHandle = nullptr;
       
        bool StartUdp(uint16_t Port, uint8_t Core);
        bool StopUdp();

        esp_netif_t* StaNetif = nullptr;

        bool NvsReady = false;
        bool NetifReady = false;
        bool EventLoopReady = false;
        bool StationInterfaceReady = false;
        bool WifiStackReady = false;
        bool CountrySet = false;
        bool EventHandlersRegistered = false;
        bool SetWifiModeDone = false;
        bool ApplyConfigDone = false;
        bool StartWifiDone = false;
        bool ApIpAcquired = false;
        bool UdpStarted = false;

        bool IsRuntimeLoggingEnabled = false;

        bool IsConnected = false;
        WifiDevice ApWifiDevice{};  

        int UdpSocket = -1;
        char MyIpAddress[16];

        uint8_t  UdpCore = 0;
        uint16_t UdpPort = 0;



    public:

        bool SetupWifi();                                             
        bool SendUdpPacket(const char* DataToSend, const char* DestinationIP, uint16_t DestinationPort);  
        size_t GetDataFromBuffer(bool* IsDataAvailable, uint8_t* DataToReceive);

        bool IsConnectedToHost() const { return IsConnected and ApIpAcquired; }
        const char* GetGatewayIpAddress() const { return ApWifiDevice.IpAddress; }
        const char* GetMyIpAddress() const { return MyIpAddress; }
        int GetNumberOfPacketsInBuffer() const { return SlotCount; }
        void SetRuntimeLogging(bool EnableRuntimeLogging) { IsRuntimeLoggingEnabled = EnableRuntimeLogging; }
};



class WifiFactory
{
    private:
        WifiFactory() = delete;

        enum class ActiveMode
        {
            None,
            Station,
            AccessPoint,
            ApSta
        };

        static ActiveMode CurrentMode;


    public:
        static Station* CreateStation(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging);
        // static AccessPoint* CreateAccessPoint(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging);
        // static ApSta* CreateApSta(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging);

};



#endif
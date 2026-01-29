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
#include "esp_netif.h"
#include "esp_netif_types.h"
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
    uint8_t HopCount;
};

struct UdpPacket
{
    int PacketLength = 0;
    char Data[128];
    char SenderIp[16];
    uint16_t SenderPort = 0;
};

struct MeshVendorIE {
    uint8_t  oui[3];     // Handshake (e.g., 0x12, 0x34, 0x56)
    uint32_t node_uid;   // CONFIG_ESP_NODE_UID
    uint8_t  hop_count;  // Current distance from Master
} __attribute__((packed));



class Station // Singleton
{
    private:

        // Factory creation only
        friend class WifiFactory;
        Station(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging);
        ~Station();


        // Event handlers
        static void WifiEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);
        
        static void IpEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);
                            
        static void UdpRxTask(void* pvParameters); 
        TaskHandle_t UdpRxTaskHandle = nullptr;


        // Wifi Configuration
        esp_err_t Error;
        wifi_init_config_t WifiDriverConfig = WIFI_INIT_CONFIG_DEFAULT();
        wifi_country_t WifiCountry = {};
        wifi_config_t WifiServiceConfig = {};
        esp_netif_t* StaNetif = nullptr;
        

        // Critical section for data access
        portMUX_TYPE CriticalSection = portMUX_INITIALIZER_UNLOCKED;


        // UDP Buffer
        uint8_t RxData[1024]{};
        uint16_t LastPositionWritten = 0;

       
        // UDP helper functions
        bool StartUdp(uint16_t Port, uint8_t Core);
        bool StopUdp();


        // Internal data
        uint8_t  UdpCore = 0;
        uint16_t UdpPort = 0;
        uint8_t SetupState = 0;
        bool SystemInitialized = false;
        bool UdpStarted = false;
        bool IsConnected = false;
        bool ApIpAcquired = false;
        bool IsRuntimeLoggingEnabled = false;
        int UdpSocket = -1;
        char MyIpAddress[16];
        WifiDevice ApWifiDevice{};  



    public:

        bool SetupWifi();                                             
        size_t SendUdpPacket(const char* DataToSend, const char* DestinationIP, uint16_t DestinationPort);  
        size_t GetDataFromBuffer(bool* IsDataAvailable, uint8_t* DataToReceive);

        bool IsConnectedToHost() const { return IsConnected && ApIpAcquired; }
        const char* GetGatewayIpAddress() const { return ApWifiDevice.IpAddress; }
        const char* GetMyIpAddress() const { return MyIpAddress; }
        void SetRuntimeLogging(bool EnableRuntimeLogging) { IsRuntimeLoggingEnabled = EnableRuntimeLogging; }
};



class AccessPointStation // Singleton
{
    private:

        // Factory creation only
        friend class WifiFactory;
        AccessPointStation(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging);
        ~AccessPointStation();

        // Event handlers
        static void ApWifiEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);

        static void StaWifiEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);                                    
        
        static void IpEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data);
                            
        static void UdpRxTask(void* pvParameters); 
        TaskHandle_t UdpRxTaskHandle = nullptr;

        // Wifi Configuration
        esp_err_t Error;
        wifi_init_config_t WifiDriverConfig = WIFI_INIT_CONFIG_DEFAULT();
        wifi_country_t WifiCountry = {};
        wifi_config_t ApWifiServiceConfig = {};
        wifi_config_t StaWifiServiceConfig = {};
        esp_netif_t* ApNetif = nullptr;
        esp_netif_t* StaNetif = nullptr;


        // Mesh logic
        uint8_t MyHopCount = 255; // Default to 'Infinity' until connected
        void UpdateBeaconMetadata(uint8_t NewHopCount);
        

        // Critical section for data access
        portMUX_TYPE CriticalSection = portMUX_INITIALIZER_UNLOCKED;


        // UDP Buffer
        uint8_t RxData[1024]{};
        uint16_t LastPositionWritten = 0;

       
        // UDP helper functions
        bool StartUdp(uint16_t Port, uint8_t Core);
        bool StopUdp();


        // Internal data
        uint8_t  UdpCore = 0;
        uint16_t UdpPort = 0;
        uint8_t SetupState = 0;
        bool SystemInitialized = false;
        bool UdpStarted = false;
        bool IsConnectedToParent = false;
        bool ApIpAcquired = false;
        bool IsRuntimeLoggingEnabled = false;
        int UdpSocket = -1;
        char MyStaIpAddress[16]; // IP given by parent
        char MyApIpAddress[16]; // IP of this AP
        WifiDevice ParentDevice{};  
        std::vector<WifiDevice> ChildDevices{};



    public:
        bool SetupWifi();                                             
        size_t SendUdpPacket(const char* DataToSend, const char* DestinationIP, uint16_t DestinationPort);  
        size_t GetDataFromBuffer(bool* IsDataAvailable, uint8_t* DataToReceive);

        bool IsConnectedToHost() const { return IsConnectedToParent && ApIpAcquired; }
        uint8_t GetHopCount() const { return MyHopCount; }
        const char* GetParentIpAddress() const { return ParentDevice.IpAddress; }
        const char* GetMyIpAddress() const { return MyStaIpAddress; }
        size_t GetNumChildren() const { return ChildDevices.size(); }
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
        static AccessPointStation* CreateAccessPointStation(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging);
};



#endif
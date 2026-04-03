#ifndef WifiClass_H
#define WifiClass_H

// Author - Ben Sturdy
// This file implements a class 'Wifi Class'. This class should be instantiated
// only once in a project. This class controls all wireless functionalities.
// This class can set up a system as an Access Point or a Station in WiFi mode. 
// This class can set up and utilise ESP-NOW. The functions with this class can 
// run on the same core as other processes.

#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_private/wifi.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h" 
#include "esp_now.h"
#include <cstddef>
#include <cstdint>
#include <string.h>
#include <sys/stat.h>
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
static constexpr size_t UDP_PACKET_SIZE = 256;
static const uint8_t MESH_OUI_0 = 0xB5;
static const uint8_t MESH_OUI_1 = 0x79;
static const uint8_t MESH_OUI_2 = 0x5B;

constexpr uint16_t PACKET_START_DELIMITER   = 0xB502;
constexpr size_t   PACKET_HEADER_SIZE       = 48;
constexpr uint16_t PACKET_END_DELIMITER     = 0x035B;

static const char* PARENT_SSID = "SturdyAP";
static const char* PARENT_PASS = "SturdyAP79";

static const char* MY_PASS = "12345678";
static const uint8_t MAX_STA_CONN = 1;
static const bool ENABLE_MASTER_CONNECTION = true;

struct WifiDevice
{
    uint64_t TimeOfConnection;
    uint64_t UID;
    uint8_t MacId[6];
    uint16_t aid;
    char IpAddress[16];
    uint8_t HopCount;
    uint8_t ChildrenCount;
    uint64_t LastHeartbeatUs;
};


#pragma pack(push, 1)
struct PacketHeader
{
    uint16_t startDelimiter;      // 0x02B5
    uint16_t payloadSize;         // bytes after header
    uint32_t reserved0;

    uint64_t slaveUid;
    uint64_t destinationUid;
    uint64_t senderTimestampUs;

    uint32_t prevCycleTimeUs;

    uint8_t  chainedSlaveCount;
    uint8_t  PacketType;
    uint8_t  flags;
    uint8_t  headerVersion;
    uint8_t  networkId;
    uint8_t  chainDistance;
    uint8_t  ttl;
    uint8_t  ForwardingMode;

    uint32_t crc32;
};
#pragma pack(pop)
static_assert(sizeof(PacketHeader) == 48, "PacketHeader must be 48 bytes");


struct UdpPacket
{
    char SenderIp[16];
    uint64_t SenderUID;
    uint16_t SenderPort;
    uint64_t ArrivalTime;
    bool IsFromParent;
    size_t PacketLength;
    uint8_t Data[UDP_PACKET_SIZE];
};

struct MeshMetadata
{
    uint8_t MacId[6];
    uint8_t HopCount;
    uint8_t ChildCount;
    bool IsValid;
};



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



        /**
         * @brief Constructor for AccessPointStation class. Never used manually, always created through the factory.
         * @param CoreToUse The core to use for UDP operations.
         * @param Port The port to use for UDP operations.
         * @param EnableRuntimeLogging Whether to enable runtime logging.
         * @return None.
         */
        AccessPointStation(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging);



        /**
         * @brief Destructor for AccessPointStation class. Cleans up resources, stops UDP if running, and resets the singleton instance pointer.
         * @return None.
         */
        ~AccessPointStation();



        /**
         * @brief Event handler for WiFi events related to the Access Point interface. 
            Handles station connection and disconnection events, updates internal state, manages child device list, and updates beacon metadata accordingly.
         * @return Void.
         */
        static void ApWifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);



        /**
         * @brief Event handler for WiFi events related to the Station interface. 
             Handles connection and disconnection events, updates internal state, manages mesh hop count, and starts/stops UDP accordingly.
         * @return Void.
         */
        static void StaWifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);       
                                    
                                    

        /**
         * @brief Event handler for IP events related to the Station interface. 
             Handles IP acquisition and loss, updates internal state, manages mesh hop count, and starts/stops UDP accordingly.
         * @return Void.
         */
        static void IpEventHandler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data);
                            


        /**
         * @brief Receive task for handling incoming UDP packets. This function runs in a FreeRTOS task and processes received data, 
             storing it in an internal buffer for later retrieval.
         * @param pvParameters 
         * @return Void.
         */
        static void ReceiveTask(void* pvParameters);
        TaskHandle_t ReceiveTaskHandle = nullptr;



        /**
         * @brief Transmit task for sending UDP packets. This function runs in a FreeRTOS task and checks for prepared packets to send, 
             transmitting them to the appropriate destination based on the current connection status.
         * @param pvParameters 
         * @return Void.
         */
        static void TransmitTask(void* pvParameters);
        TaskHandle_t TransmitTaskHandle = nullptr;



        /**
         * @brief Mesh task for handling mesh-specific operations such as scanning for parent nodes, managing hop counts, and updating beacon metadata. 
             This function runs in a FreeRTOS task and performs periodic checks and updates related to the mesh network.
         * 
         * @param pvParameters 
         */
        static void MeshTask(void* pvParameters); 
        TaskHandle_t MeshTaskHandle = nullptr;



        /**
         * @brief   Callback function for handling vendor-specific Information Elements (IEs) received during WiFi scanning. This function processes the IEs to extract mesh-related metadata such as hop count and child count, and updates an internal cache for use in AP selection logic.
         * @param ctx Context pointer (not used in this implementation).
         * @param type The type of the vendor IE (e.g., beacon, probe response).
         * @param sa The source MAC address of the device that sent the IE.
         * @param vnd_ie Pointer to the vendor IE data structure containing the OUI and payload.
         * @param rssi The RSSI value of the received IE, which can be used for logging or decision-making.
         * @return Void.
         */
        static void WifiVendorIeCb(void *ctx, wifi_vendor_ie_type_t type, const uint8_t sa[6], const vendor_ie_data_t *vnd_ie, int rssi);



        /**
         * @brief Initiates a mesh scan to discover nearby mesh nodes.
         * @return bool: True if the scan was successfully initiated, false otherwise. The scan is non-blocking, and results will be processed in the event handler when the scan completes.
         */
        bool InitiateMeshScan();



        /**
         * @brief Updates the beacon metadata with the current hop count and child count. This function constructs a vendor-specific IE with the mesh information and sets it for both beacon and probe response frames. This allows other devices scanning for WiFi networks to receive up-to-date information about this node's position in the mesh network.
         * @param Hop 
         * @param Children 
         * @return Void.
         */
        void UpdateBeaconMetadata(uint8_t Hop, uint8_t Children);



        /**
         * @brief Parses the results of a WiFi scan to identify potential parent nodes for the mesh network. This function processes the list of scanned APs, checks for the presence of the custom mesh IE, and extracts metadata such as hop count and child count. Based on this information, it determines if there is a better parent node to connect to and updates internal state accordingly.
         * @return Void.
         */
        void ParseScanResults();



        /**
         * @brief Connects to the best available parent AP based on the results of the WiFi scan and the extracted mesh metadata. This function evaluates potential parent nodes, compares their hop counts and child counts, and initiates a connection to the most suitable parent AP to optimize the mesh network topology.
         * @return Void.
         */
        void ConnectToBestAp();



        /** 
         * @brief Creates a mesh packet with the specified data and metadata. Called in other methods.
         * @param DataToInclude Pointer to the data to include in the packet.
         * @param DataLength Length of the data to include.
         * @param PacketType Type of the packet to create.
         * @param PacketOut Pointer to the buffer where the packet will be stored.
         * @param OutputBufferSize Size of the output buffer.
         * @return The size of the created packet, or 0 if creation failed.
         */
        size_t CreatePacket(const uint8_t* DataToInclude,
                            size_t DataLength,
                            uint8_t PacketType,
                            uint8_t* PacketOut,
                            size_t OutputBufferSize);



        /**
         * @brief Processes received data from the UDP buffer, extracting complete packets and handling them according to their type. This function is called by the receive task when new data is available, and it manages the internal state of received packets, ensuring that they are properly parsed and processed.
         * @param data 
         * @param length 
         * @return Void.
         */
        void ProcessData(uint8_t* data, int length);



        /**
         * @brief Prepares a packet for transmission by taking the data to include, creating the appropriate packet structure, and storing it in an internal buffer for the transmit task to send. This function handles the critical section for preparing the packet and ensures that the transmit task can safely access the prepared packet when it is ready to be sent.
         * @param rxData 
         * @param rxLength 
         * @param txBuffer 
         * @param txLength 
         * @return size_t: The size of the prepared packet, or 0 if preparation failed.
         */
        size_t PrepareTxPacket(const uint8_t* rxData, int rxLength, uint8_t* txBuffer, int& txLength);
        


        /**
         * @brief Determines the destination address for a packet based on its forwarding mode and sender address
         * @param SourceAddress 
         * @param Data 
         * @param DataLength 
         * @param DestinationAddress 
         * @return bool: True if a valid destination address was determined, false otherwise. The DestinationAddress parameter will be populated with the appropriate address if true is returned.
         */
        bool DetermineDestinationAddress(const sockaddr_in& SourceAddress, const uint8_t* Data, int DataLength, sockaddr_in& DestinationAddress);



        /**
         * @brief Helper function to start all UDP-based services
         * @param Port 
         * @param Core 
         * @return bool: True if UDP services were successfully started, false otherwise.
         */
        bool StartUdp(uint16_t Port, uint8_t Core);



        /**
         * @brief Helper function to stop all UDP-based services, including closing sockets and deleting tasks.
         * @return bool: True if UDP services were successfully stopped, false otherwise.
         */
        bool StopUdp();



        // Wifi Configuration
        esp_err_t Error;
        wifi_init_config_t WifiDriverConfig = WIFI_INIT_CONFIG_DEFAULT();
        wifi_country_t WifiCountry = {};
        wifi_config_t ApWifiServiceConfig = {};
        wifi_config_t StaWifiServiceConfig = {};
        esp_netif_t* ApNetif = nullptr;
        esp_netif_t* StaNetif = nullptr;








        MeshMetadata CallbackIeData[20]{};
        uint8_t MyHopCount = 255; // Default to 'Infinity' until connected

        wifi_ap_record_t CandidateWifiRecord{};
        wifi_ap_record_t ParentWifiRecord{};
        bool IsCandidateValid = false;
        bool IsCandidateMaster = false;
        bool RoamRequested = false;
        uint8_t CandidateHop = 0;
        uint8_t CandidateChildren = 0;
        bool IsMasterFound = false;
        bool IsScanning = false;
        bool IsConnecting = false;
        
        
        

        static constexpr int MaxPayload = 1024;      // pick your limit
        uint8_t LatestPayload[MaxPayload]{};
        uint16_t LatestPayloadSize = 0;
        uint8_t  LatestPayloadType = 0;
        int64_t  LatestPayloadUs = 0;
        volatile uint32_t PayloadSeq = 0;            // for race-safe getter later
        uint8_t PreparedTxPacket[MaxPayload]{};
        int PreparedTxLength = 0;
        portMUX_TYPE TxCriticalSection = portMUX_INITIALIZER_UNLOCKED;











        // Critical section for data access
        portMUX_TYPE CriticalSection = portMUX_INITIALIZER_UNLOCKED;


        // UDP Buffer
        UdpPacket PacketBuffer[UDP_SLOTS];
        volatile size_t Head = 0;
        volatile size_t Tail = 0;

       
        // UDP helper functions
        


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
        volatile int64_t LastHeartbeatUs = 0;
        WifiDevice ParentDevice{};  
        std::vector<WifiDevice> ChildDevices{};



    public:

        /**
         * @brief Setup the WiFi system as an Access Point + Station (Mesh Node). This function initializes the WiFi driver, configures the device as both an AP and a STA, registers event handlers, and starts the necessary FreeRTOS tasks for handling WiFi events, UDP communication, and mesh operations.
         * @return bool: True if setup was successful, false otherwise. Note that this function may return true even if the device is not currently connected to a parent AP, as it may still be in the process of scanning and connecting.
         */
        bool SetupWifi();     
        

        
        /**
         * @brief Transmit a UDP packet to a specified destination IP and port. This function prepares the packet and signals the transmit task to send it.
         * @param TxData Pointer to the data to be transmitted.
         * @param TxLength Length of the data to be transmitted.
         * @param DestinationAddress The sockaddr_in structure containing the destination IP and port.
         * @return size_t: The length of the data that was sent, or 0 if the system did not send the data (e.g., due to system errors). 
         */
        size_t SendData(const uint8_t* TxData, int TxLength, const sockaddr_in& DestinationAddress);



        /**
         * @brief Retrieve received UDP data from the internal buffer. This function checks if there are any packets available, and if so, copies the data to the provided buffer and returns the length of the data.
         * @param DataToReceive Buffer where the received data will be copied if available.
         * @return size_t: The length of the data that was copied to the output buffer, or 0 if no data was available.
         */
        size_t GetDataFromBuffer(UdpPacket* DataToReceive);



        /**
         * @brief Get the Hop Count of this node in the mesh network. The Hop Count represents the number of hops to the root node (or master). A value of 255 indicates that the node is not currently connected to a parent and is effectively "infinite" hops away from the root.
         * @return uint8_t: The current Hop Count of this node, or 255 if not connected to a parent.
         */
        uint8_t GetHopCount() const { return MyHopCount; }



        /**
         * @brief Check if the device is currently connected to a parent AP (host) and has acquired an IP address. This indicates that the device is successfully part of the mesh network and can communicate with other nodes.
         * @return bool: True if the device is connected to a parent and has an IP address, false otherwise.
         */
        bool IsConnectedToHost() const { return IsConnectedToParent && ApIpAcquired; }
        
        
        
        /**
         * @brief Get the Parent Ip Address object. This is the IP address of the AP that this device is currently connected to as a station. If the device is not currently connected to a parent, this will return an empty string or an invalid IP.
         * @return const char*: The IP address of the parent AP, or an empty string if not connected.
         */
        const char* GetParentIpAddress() const { return ParentDevice.IpAddress; }



        /**
         * @brief Get the IP address of this device as a station. This is the IP address assigned to this device by the parent AP. If the device is not currently connected to a parent, this will return an empty string or an invalid IP.
         * @return const char*: The IP address of this device as a station, or an empty string if not connected.
         */
        const char* GetMyIpAddress() const { return MyStaIpAddress; }



        /**
         * @brief Get the number of child devices currently connected to this device's AP. This indicates how many other devices are currently connected to this node as their parent in the mesh network.
         * @return size_t: The number of child devices currently connected, or 0 if no devices are connected.
         */
        size_t GetNumChildren() const { return ChildDevices.size(); }



        /**
         * @brief Enable or disable runtime logging for this class. When enabled, the class will output informational and error logs to the console using ESP_LOGI and ESP_LOGE. This can be useful for debugging and monitoring the behavior of the mesh network, especially during development and testing.
         * @param EnableRuntimeLogging: Set to true to enable logging, or false to disable logging.
         * @return void.
         */
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
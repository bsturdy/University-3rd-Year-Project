#include "WifiClass.h"
#include "esp_wifi_types_generic.h"
#include "portmacro.h"
#include <cstddef>
#include <string.h>

// Author - Ben Sturdy
// This file implements a class 'Wifi Class'. This class should be instantiated
// only once in a project. This class controls all wireless functionalities.
// This class can set up a system as an Access Point or a Station in WiFi mode. 
// This class can set up and utilise ESP-NOW. The functions with this class can 
// run on the same core as other processes.





//==============================================================================//
//                                                                              //
//                                 Station                                      //
//                                                                              //
//==============================================================================// 

#define STA_TAG "Station"

static Station* StaClassInstance;

Station::Station(uint8_t CoreToUse, uint16_t Port, bool EnableRuntimeLogging)
{
    StaClassInstance = this;
    UdpCore = CoreToUse;
    UdpPort = Port;
    IsRuntimeLoggingEnabled = EnableRuntimeLogging;

    IsConnected = false;
    ApIpAcquired = false;
    memset(&ApWifiDevice, 0, sizeof(ApWifiDevice));
    memset(&MyIpAddress, 0, sizeof(MyIpAddress));
}

Station::~Station()
{
    ;
}

void Station::WifiEventHandler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data)
{
    if (StaClassInstance == nullptr) return;
    if (event_base != WIFI_EVENT) return;


    switch (event_id)
    {
        case WIFI_EVENT_STA_START:
        {
            // Internal state update
            StaClassInstance->IsConnected = false;
            StaClassInstance->ApIpAcquired = false;

            // Stop UDP system
            StaClassInstance->StopUdp();

            // Initialize wifi data
            memset(&StaClassInstance->ApWifiDevice, 0, sizeof(StaClassInstance->ApWifiDevice));
            memset(StaClassInstance->MyIpAddress, 0, sizeof(StaClassInstance->MyIpAddress));

            // Attempt to connect to AP
            esp_wifi_connect();

            // Logging
            if (StaClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGW(STA_TAG, "STA started, connecting...");
            }

            break;
        }



        case WIFI_EVENT_STA_STOP:
        {
            // Stop UDP system
            StaClassInstance->StopUdp();

            // Internal state update
            StaClassInstance->IsConnected = false;
            StaClassInstance->ApIpAcquired = false;

            // Reset wifi data
            memset(&StaClassInstance->ApWifiDevice, 0, sizeof(StaClassInstance->ApWifiDevice));
            memset(StaClassInstance->MyIpAddress, 0, sizeof(StaClassInstance->MyIpAddress));

            // Logging
            if (StaClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGW(STA_TAG, "STA stopped");
            }

            break;
        }



        case WIFI_EVENT_STA_CONNECTED:
        {
            // Parse data to appropriate Event structure
            wifi_event_sta_connected_t* Event = static_cast<wifi_event_sta_connected_t*>(event_data);

            // Internal state update
            StaClassInstance->IsConnected = true;
            StaClassInstance->ApIpAcquired = false;

            // Create AP wifi device with event data
            StaClassInstance->ApWifiDevice.TimeOfConnection = esp_timer_get_time();
            StaClassInstance->ApWifiDevice.IpAddress[0] = '\0';
            StaClassInstance->ApWifiDevice.aid = Event->aid;
            memcpy(StaClassInstance->ApWifiDevice.MacId, Event->bssid, 6);

            // Logging
            if (StaClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGW(STA_TAG,
                         "STA connected\nTime Of Connection: %llu | MacID: " MACSTR " | AID: %d",
                         StaClassInstance->ApWifiDevice.TimeOfConnection,
                         MAC2STR(StaClassInstance->ApWifiDevice.MacId),
                         StaClassInstance->ApWifiDevice.aid);
            }

            break;
        }



        case WIFI_EVENT_STA_DISCONNECTED:
        {
            // Parse data to appropriate Event structure
            wifi_event_sta_disconnected_t* Event = static_cast<wifi_event_sta_disconnected_t*>(event_data);

            // Stop UDP system
            StaClassInstance->StopUdp();

            // Internal state update
            StaClassInstance->IsConnected = false;
            StaClassInstance->ApIpAcquired = false;

            // Wipe wifi data
            memset(&StaClassInstance->ApWifiDevice, 0, sizeof(StaClassInstance->ApWifiDevice));
            memset(StaClassInstance->MyIpAddress, 0, sizeof(StaClassInstance->MyIpAddress));

            // Logging
            if (StaClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGW(STA_TAG, "STA disconnected\nReason = %d | RSSI = %d",
                         Event->reason, Event->rssi);
            }

            // Immediate reconnect attempt
            esp_wifi_connect();

            break;
        }



        default:
            break;
    }
}

void Station::IpEventHandler(void* arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data)
{
    if (StaClassInstance == nullptr) return;
    if (event_base != IP_EVENT) return;


    switch (event_id)
    {
        case IP_EVENT_STA_GOT_IP:
        {
            // Parse data to appropriate Event structure
            ip_event_got_ip_t* Event = static_cast<ip_event_got_ip_t*>(event_data);

            // Both gateway IP and device IP strings
            char GwStr[16] = {0};
            esp_ip4addr_ntoa(&Event->ip_info.gw, GwStr, sizeof(GwStr));
            char MyStr[16] = {0};
            esp_ip4addr_ntoa(&Event->ip_info.ip, MyStr, sizeof(MyStr));

            // Store gateway IP and device IP
            strncpy(StaClassInstance->ApWifiDevice.IpAddress, GwStr,
                    sizeof(StaClassInstance->ApWifiDevice.IpAddress) - 1);
            StaClassInstance->ApWifiDevice.IpAddress[sizeof(StaClassInstance->ApWifiDevice.IpAddress) - 1] = '\0';
            strncpy(StaClassInstance->MyIpAddress, MyStr,
                    sizeof(StaClassInstance->MyIpAddress) - 1);
            StaClassInstance->MyIpAddress[sizeof(StaClassInstance->MyIpAddress) - 1] = '\0';

            // Internal state update
            StaClassInstance->ApIpAcquired = true;

            bool UdpStartedOk = StaClassInstance->StartUdp(StaClassInstance->UdpPort, StaClassInstance->UdpCore);

            // Logging
            if (StaClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGW(STA_TAG, "STA got IP\nIP = %s | GW(AP) = %s", StaClassInstance->MyIpAddress, StaClassInstance->ApWifiDevice.IpAddress);

                if (UdpStartedOk)
                {
                    ESP_LOGW(STA_TAG, "UDP system started on port %d", StaClassInstance->UdpPort);
                }
                else
                {
                    ESP_LOGE(STA_TAG, "Failed to start UDP system");
                }
            }

            break;
        }



        case IP_EVENT_STA_LOST_IP:
        {
            // Internal state update
            StaClassInstance->ApIpAcquired = false;

            // Wipe all IP info
            memset(StaClassInstance->ApWifiDevice.IpAddress, 0, sizeof(StaClassInstance->ApWifiDevice.IpAddress));
            memset(StaClassInstance->MyIpAddress, 0, sizeof(StaClassInstance->MyIpAddress));

            // Stop UDP system
            bool UdpStoppedOk = StaClassInstance->StopUdp();

            // Logging
            if (StaClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGW(STA_TAG, "STA lost IP");

                if (UdpStoppedOk)
                {
                    ESP_LOGW(STA_TAG, "UDP system stopped");
                }
                else
                {
                    ESP_LOGE(STA_TAG, "Failed to stop UDP system");
                }
            }

            break;
        }



        default:
            break;
    }
}

bool Station::StartUdp(uint16_t Port, uint8_t Core)
{
    if (StaClassInstance->UdpStarted) return true;
    if (!StaClassInstance->IsConnected) return false;  
    if (!StaClassInstance->ApIpAcquired) return false; 
    if (Port == 0) return false;

    // Create socket
    StaClassInstance->UdpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (StaClassInstance->UdpSocket < 0)
    {
        StaClassInstance->UdpSocket = -1;
        return false;
    }

    // Bind
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(Port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(StaClassInstance->UdpSocket, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        close(StaClassInstance->UdpSocket);
        StaClassInstance->UdpSocket = -1;
        return false;
    }

    // Non-blocking-ish receive (polling loop)
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 1000; // divide by 1000 for milliseconds
    setsockopt(StaClassInstance->UdpSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));


    // Create RX task
    if (xTaskCreatePinnedToCore(&Station::UdpRxTask,
                                "StaUdpRx",
                                4096,
                                nullptr,          // we use StaClassInstance singleton
                                5,
                                &StaClassInstance->UdpRxTaskHandle,
                                Core) != pdPASS)
    {
        close(StaClassInstance->UdpSocket);
        StaClassInstance->UdpSocket = -1;
        StaClassInstance->UdpRxTaskHandle = nullptr;
        return false;
    }

    StaClassInstance->UdpStarted = true;
    return true;
}

bool Station::StopUdp()
{
    if (!StaClassInstance->UdpStarted) return true;

    // Mark stopped first so UdpRxTask can exit on its next timeout
    StaClassInstance->UdpStarted = false;

    // Delete RX task (if running)
    if (StaClassInstance->UdpRxTaskHandle != nullptr)
    {
        vTaskDelete(StaClassInstance->UdpRxTaskHandle);
        StaClassInstance->UdpRxTaskHandle = nullptr;
    }

    // Close socket (if open)
    if (StaClassInstance->UdpSocket >= 0)
    {
        shutdown(StaClassInstance->UdpSocket, SHUT_RDWR);
        close(StaClassInstance->UdpSocket);
        StaClassInstance->UdpSocket = -1;
    }

    return true;
}

void Station::UdpRxTask(void* pvParameters)
{
    (void)pvParameters;

    //uint8_t RxBuffer[UDP_SLOT_SIZE];
    uint8_t TempBuffer[1024];
    sockaddr_in SourceAddr;
    socklen_t AddrLen = sizeof(SourceAddr);

    while (true)
    {
        if (StaClassInstance == nullptr) break;
        if (!StaClassInstance->UdpStarted) break;
        if (StaClassInstance->UdpSocket < 0) break;

        AddrLen = sizeof(SourceAddr);

        int BytesReceived = recvfrom(
            StaClassInstance->UdpSocket,
            TempBuffer,
            sizeof(TempBuffer),
            0,
            (struct sockaddr*)&SourceAddr,
            &AddrLen
        );
        
        if (BytesReceived > 0)
        {
            portENTER_CRITICAL(&StaClassInstance->CriticalSection);

            size_t Remaining = sizeof(StaClassInstance->RxData) - StaClassInstance->LastPositionWritten;

            // Typecast to avoid signed/unsigned comparison
            size_t n = (size_t)BytesReceived;

            if (n <= Remaining)
            {
                memcpy(StaClassInstance->RxData + StaClassInstance->LastPositionWritten, TempBuffer, n);

                StaClassInstance->LastPositionWritten += n;
            }

            portEXIT_CRITICAL(&StaClassInstance->CriticalSection);
        }

        vTaskDelay(1);

    }

    vTaskDelete(nullptr);
}

bool Station::SetupWifi()
{
    switch (SetupState) 
    {
        case 0: // NVS
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: nvs_flash_init()");
            Error = nvs_flash_init();
            if (Error == ESP_ERR_NVS_NO_FREE_PAGES || Error == ESP_ERR_NVS_NEW_VERSION_FOUND)
            {
                if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: nvs_flash_erase()");
                if (nvs_flash_erase() != ESP_OK) return false;
                if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: nvs_flash_init() retry");
                Error = nvs_flash_init();
            }
            if (Error != ESP_OK) return false;
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: NVS ready");
            SetupState++;
            break;

            

        case 1: // Netif
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: esp_netif_init()");
            if (esp_netif_init() != ESP_OK) return false;
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: Netif ready");
            SetupState++;
            break;



        case 2: // Event loop
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: esp_event_loop_create_default()");
            if (esp_event_loop_create_default() != ESP_OK) return false;
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: Event loop ready");
            SetupState++;
            break;



        case 3: // Create STA interface
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: esp_netif_create_default_wifi_sta()");
            StaNetif = esp_netif_create_default_wifi_sta();
            if (StaNetif == nullptr) return false;
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: Station interface ready");
            SetupState++;
            break;



        case 4: // Wi-Fi init
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: esp_wifi_init()");
            if (esp_wifi_init(&WifiDriverConfig) != ESP_OK) return false;
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: Wi-Fi stack ready");
            SetupState++;
            break;



        case 5: // Country
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: esp_wifi_set_country(GB)");
            memcpy(WifiCountry.cc, "GB", 2);
            WifiCountry.schan = 1;
            WifiCountry.nchan = 13;
            WifiCountry.max_tx_power = 20;
            WifiCountry.policy = WIFI_COUNTRY_POLICY_AUTO;
            if (esp_wifi_set_country(&WifiCountry) != ESP_OK) return false;
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: Country set");
            SetupState++;
            break;



        case 6: // Register event handlers
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: register WIFI_EVENT handler");
            if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    &Station::WifiEventHandler, nullptr, nullptr) != ESP_OK)
                return false;
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: register IP_EVENT handler");
            if (esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                                    &Station::IpEventHandler, nullptr, nullptr) != ESP_OK)
                return false;
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: Event handlers registered");
            SetupState++;
            break;



        case 7: // Configure STA
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: build wifi_config_t");
            strncpy((char*)WifiServiceConfig.sta.ssid, CONFIG_ESP_WIFI_SSID, sizeof(WifiServiceConfig.sta.ssid) - 1);
            strncpy((char*)WifiServiceConfig.sta.password, CONFIG_ESP_WIFI_PASSWORD, sizeof(WifiServiceConfig.sta.password) - 1);
            WifiServiceConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            WifiServiceConfig.sta.pmf_cfg.capable = true;
            WifiServiceConfig.sta.pmf_cfg.required = false;
            SetupState++;
            break;



        case 8: // Set mode
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: esp_wifi_set_mode(STA)");
            if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: Wi-Fi mode set");
            SetupState++;
            break;



        case 9: // Apply config
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: esp_wifi_set_config(STA)");
            if (esp_wifi_set_config(WIFI_IF_STA, &WifiServiceConfig) != ESP_OK) return false;
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: Config applied");
            SetupState++;
            break;



        case 10: // Start Wi-Fi
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: esp_wifi_start()");
            if (esp_wifi_start() != ESP_OK) return false;
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, "SetupWifi: Wi-Fi started");
            SetupState = 100;
            break;



        case 100: // Done
            SystemInitialized = true;
            break;



        default:
            return false;
            break;
    }

    if (SetupState == 100) 
    {
        return true;
    }
    else 
    {
        return false;
    }
}

size_t Station::GetDataFromBuffer(bool* IsDataAvailable, uint8_t* DataToReceive)
{
    if (IsDataAvailable) *IsDataAvailable = false;
    if (!IsDataAvailable || !DataToReceive) return 0;

    size_t Copied = 0;

    portENTER_CRITICAL(&CriticalSection);

    const size_t Available = LastPositionWritten;

    if (Available > 0)
    {
        memcpy(DataToReceive, RxData, Available);

        Copied = Available;

        LastPositionWritten = 0;

        *IsDataAvailable = true;
    }
    else
    {
        LastPositionWritten = 0;
    }

    portEXIT_CRITICAL(&CriticalSection);

    return Copied;

}

size_t Station::SendUdpPacket(const char* Data, const char* DestinationIP, uint16_t DestinationPort)
{
    if (!Data || !DestinationIP) return (size_t)-1;
    if (!UdpStarted) return (size_t)-1;
    if (UdpSocket < 0) return (size_t)-1;
    if (DestinationPort == 0) return (size_t)-1;

    sockaddr_in DestAddr{};
    DestAddr.sin_family = AF_INET;
    DestAddr.sin_port = htons(DestinationPort);

    if (inet_pton(AF_INET, DestinationIP, &DestAddr.sin_addr) != 1)
    {
        return (size_t)-1;
    }

    ssize_t Sent = sendto(
        UdpSocket,
        Data,
        strlen(Data),
        0,
        reinterpret_cast<sockaddr*>(&DestAddr),
        sizeof(DestAddr)
    );

    return Sent;
}





//==============================================================================//
//                                                                              //
//                                AP + STA                                      //
//                                                                              //
//==============================================================================// 

#define STA_TAG "Station"

static AccessPointStation* ApStaClassInstance;

AccessPointStation::AccessPointStation(uint8_t CoreToUse, uint16_t Port, bool EnableRuntimeLogging)
{
    ApStaClassInstance = this;
    UdpCore = CoreToUse;
    UdpPort = Port;
    IsRuntimeLoggingEnabled = EnableRuntimeLogging;

    SystemInitialized = false;
    UdpStarted = false;
    IsConnectedToParent = false;
    ApIpAcquired = false;
    MyHopCount = 255; // Default to 'Infinity' until scan/connect
}

AccessPointStation::~AccessPointStation()
{
    ;
}

void AccessPointStation::ApWifiEventHandler(void* arg, esp_event_base_t event_base,
                                           int32_t event_id, void* event_data)
{
    if (ApStaClassInstance == nullptr || event_base != WIFI_EVENT) return;

    if (event_id == WIFI_EVENT_AP_STACONNECTED) 
    {
        wifi_event_ap_staconnected_t* Event = (wifi_event_ap_staconnected_t*)event_data;
        
        WifiDevice child;
        memset(&child, 0, sizeof(WifiDevice)); // Precise: Clear memory for string safety
        child.TimeOfConnection = esp_timer_get_time();
        child.aid = Event->aid;
        child.HopCount = 255; // Initialized as unknown
        memcpy(child.MacId, Event->mac, 6);
        child.IpAddress[0] = '\0'; 

        ApStaClassInstance->ChildDevices.push_back(child);

        if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
            ESP_LOGW("MESH_AP", "Child Joined | MAC: " MACSTR " | AID: %d", 
                     MAC2STR(Event->mac), Event->aid);
        }
    } 
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) 
    {
        wifi_event_ap_stadisconnected_t* Event = (wifi_event_ap_stadisconnected_t*)event_data;
        
        if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
            ESP_LOGE("MESH_AP", "Child Left | MAC: " MACSTR, MAC2STR(Event->mac));
        }

        // Precise removal using Erase-Remove Idiom
        auto& list = ApStaClassInstance->ChildDevices;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const WifiDevice& d) {
            return memcmp(d.MacId, Event->mac, 6) == 0;
        }), list.end());
    }
}

void AccessPointStation::StaWifiEventHandler(void* arg, esp_event_base_t event_base,
                                            int32_t event_id, void* event_data)
{
    if (ApStaClassInstance == nullptr || event_base != WIFI_EVENT) return;

    switch (event_id) 
    {
        case WIFI_EVENT_STA_START:
            ApStaClassInstance->IsConnectedToParent = false;
            ApStaClassInstance->ApIpAcquired = false;
            ApStaClassInstance->StopUdp();
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED: 
        {
            wifi_event_sta_connected_t* Event = static_cast<wifi_event_sta_connected_t*>(event_data);
            ApStaClassInstance->IsConnectedToParent = true;
            
            ApStaClassInstance->ParentDevice.TimeOfConnection = esp_timer_get_time();
            ApStaClassInstance->ParentDevice.aid = Event->aid;
            memcpy(ApStaClassInstance->ParentDevice.MacId, Event->bssid, 6);

            if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
                ESP_LOGW(STA_TAG, "Hardware Link to Parent Established");
            }
            break;
        }

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            // Precise State Reset
            ApStaClassInstance->IsConnectedToParent = false;
            ApStaClassInstance->ApIpAcquired = false;
            
            // Critical Mesh Logic: Poison the path so children drop off and re-scan
            ApStaClassInstance->MyHopCount = 255; 
            ApStaClassInstance->UpdateBeaconMetadata(255); 
            
            ApStaClassInstance->StopUdp();
            
            if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
                 wifi_event_sta_disconnected_t* Event = (wifi_event_sta_disconnected_t*)event_data;
                 ESP_LOGE(STA_TAG, "Parent Lost (Reason: %d). Reconnecting...", Event->reason);
            }

            // Attempt to re-establish the link
            esp_wifi_connect();
            break;
        }
    }
}

void AccessPointStation::IpEventHandler(void* arg, esp_event_base_t event_base,
                                         int32_t event_id, void* event_data)
{
    if (ApStaClassInstance == nullptr || event_base != IP_EVENT) return;

    switch (event_id)
    {
        case IP_EVENT_STA_GOT_IP:
        {
            ip_event_got_ip_t* Event = static_cast<ip_event_got_ip_t*>(event_data);

            // 1. Convert IP addresses to strings
            char GwStr[16] = {0};
            esp_ip4addr_ntoa(&Event->ip_info.gw, GwStr, sizeof(GwStr));
            char MyStr[16] = {0};
            esp_ip4addr_ntoa(&Event->ip_info.ip, MyStr, sizeof(MyStr));

            // 2. Store internal station data
            strncpy(ApStaClassInstance->ParentDevice.IpAddress, GwStr, 15);
            strncpy(ApStaClassInstance->MyStaIpAddress, MyStr, 15);
            
            // 3. Update State Flags
            ApStaClassInstance->ApIpAcquired = true;
            ApStaClassInstance->IsConnectedToParent = true;

            // 4. MESH LOGIC: Path Validation
            // If connected to a Mesh node, increment. 
            // If connected to a standard router (255), we assume it's the Root (0) and we become 1.
            if (ApStaClassInstance->ParentDevice.HopCount != 255) 
            {
                ApStaClassInstance->MyHopCount = ApStaClassInstance->ParentDevice.HopCount + 1;
            }
            else 
            {
                 ApStaClassInstance->MyHopCount = 1; 
                 ApStaClassInstance->ParentDevice.HopCount = 0; 
            }

            // 5. Broadcast our new status (Host + 1)
            ApStaClassInstance->UpdateBeaconMetadata(ApStaClassInstance->MyHopCount);

            // 6. Start UDP
            bool UdpStartedOk = ApStaClassInstance->StartUdp(ApStaClassInstance->UdpPort, ApStaClassInstance->UdpCore);

            // 7. Simple Runtime Logging
            if (ApStaClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGI(STA_TAG, "STA Connected. IP: %s, GW: %s, My Hop: %d", MyStr, GwStr, ApStaClassInstance->MyHopCount);
                if (!UdpStartedOk) ESP_LOGE(STA_TAG, "UDP failed to start on port %d", ApStaClassInstance->UdpPort);
            }
            break;
        }
 


        case IP_EVENT_AP_STAIPASSIGNED:
        {
            // Get the IP that was just assigned
            ip_event_ap_staipassigned_t* Event = static_cast<ip_event_ap_staipassigned_t*>(event_data);
            
            char AssignedIp[16];
            esp_ip4addr_ntoa(&Event->ip, AssignedIp, sizeof(AssignedIp));


            // In a mesh, the most recent device to connect is the one getting the IP.
            // We search our vector from NEWEST to OLDEST (reverse iterator).
            bool matched = false;
            for (auto it = ApStaClassInstance->ChildDevices.rbegin(); it != ApStaClassInstance->ChildDevices.rend(); ++it)
            {
                // If the IpAddress is still empty, this is our target.
                if (it->IpAddress[0] == '\0') 
                {
                    strncpy(it->IpAddress, AssignedIp, sizeof(it->IpAddress) - 1);
                    it->IpAddress[sizeof(it->IpAddress) - 1] = '\0';
                    
                    if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
                        ESP_LOGW("MESH_AP", "Linked IP %s to Child MAC " MACSTR, AssignedIp, MAC2STR(it->MacId));
                    }
                    matched = true;
                    break;
                }
            }

            if (!matched && ApStaClassInstance->IsRuntimeLoggingEnabled) {
                ESP_LOGE("MESH_AP", "Received IP assignment %s but found no matching child!", AssignedIp);
            }
            break;
        }


        
        case IP_EVENT_STA_LOST_IP:
        {
            ApStaClassInstance->ApIpAcquired = false;
            memset(ApStaClassInstance->MyStaIpAddress, 0, 16);

            // MESH LOGIC: Poison the route
            ApStaClassInstance->MyHopCount = 255; 
            ApStaClassInstance->UpdateBeaconMetadata(255);

            ApStaClassInstance->StopUdp();
            
            if (ApStaClassInstance->IsRuntimeLoggingEnabled) ESP_LOGE(STA_TAG, "STA lost IP");
            break;
        }
    }
}

void AccessPointStation::UpdateBeaconMetadata(uint8_t NewHopCount) 
{
    MeshVendorIE payload;
    
    // Identify our specific mesh traffic
    payload.oui[0] = 0x12; 
    payload.oui[1] = 0x34;
    payload.oui[2] = 0x56;

    // Set the current state
    payload.node_uid = CONFIG_ESP_NODE_UID;
    payload.hop_count = NewHopCount;

    // Prepare the IDF container
    // Size is the struct + 2 bytes for Element ID and Length headers
    vendor_ie_data_t* ie_data = (vendor_ie_data_t*)malloc(sizeof(vendor_ie_data_t) + sizeof(MeshVendorIE));
    if (ie_data == nullptr) return;

    ie_data->element_id = WIFI_VENDOR_IE_ELEMENT_ID;
    ie_data->length = sizeof(MeshVendorIE);
    memcpy(ie_data->vendor_oui, payload.oui, 3);
    memcpy(ie_data->payload, &payload, sizeof(MeshVendorIE));

    // 4. Update the radio (WIFI_VND_IE_ID_0 is our slot)
    esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, ie_data);
    esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_1, ie_data);

    free(ie_data);
    
    if (IsRuntimeLoggingEnabled) {
        ESP_LOGI("MESH_IE", "Broadcast updated: UID %lu, Hop %d", payload.node_uid, NewHopCount);
    }
}

bool AccessPointStation::StartUdp(uint16_t Port, uint8_t Core)
{
    // Use our singleton instance
    if (ApStaClassInstance->UdpStarted) return true;
    
    // Multi-interface connection check
    // We only start UDP if we have an IP (either as an AP or a STA)
    if (!ApStaClassInstance->IsConnectedToParent && ApStaClassInstance->ChildDevices.empty()) 
    {
        // Optimization: In a mesh, you might want to start UDP even if not connected to a parent
        // so you can talk to your children. I'll stick to your "Must have IP" logic.
        if (!ApStaClassInstance->ApIpAcquired) return false;
    }
    
    if (Port == 0) return false;

    // Create socket
    ApStaClassInstance->UdpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (ApStaClassInstance->UdpSocket < 0)
    {
        ApStaClassInstance->UdpSocket = -1;
        return false;
    }

    // Bind
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(Port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // Listens on both AP and STA interfaces

    if (bind(ApStaClassInstance->UdpSocket, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        close(ApStaClassInstance->UdpSocket);
        ApStaClassInstance->UdpSocket = -1;
        return false;
    }

    // Socket Timeout (Essential for the RX task loop to breathe)
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000; // Increased to 10ms for better CPU efficiency in dual-mode
    setsockopt(ApStaClassInstance->UdpSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Task Management
    // Ensure the handle is null before creating a new one
    if (ApStaClassInstance->UdpRxTaskHandle != nullptr) {
        vTaskDelete(ApStaClassInstance->UdpRxTaskHandle);
        ApStaClassInstance->UdpRxTaskHandle = nullptr;
    }

    if (xTaskCreatePinnedToCore(&AccessPointStation::UdpRxTask,
                                "ApStaUdpRx",
                                4096,
                                nullptr,
                                5,
                                &ApStaClassInstance->UdpRxTaskHandle,
                                Core) != pdPASS)
    {
        close(ApStaClassInstance->UdpSocket);
        ApStaClassInstance->UdpSocket = -1;
        ApStaClassInstance->UdpRxTaskHandle = nullptr;
        return false;
    }

    ApStaClassInstance->UdpStarted = true;
    if (ApStaClassInstance->IsRuntimeLoggingEnabled) ESP_LOGI("UDP", "Mesh UDP Started on Port %d", Port);
    
    return true;
}

bool AccessPointStation::StopUdp()
{
    if (!ApStaClassInstance->UdpStarted) return true;

    // Mark as stopped immediately
    // This tells any other logic that the UDP path is no longer valid
    ApStaClassInstance->UdpStarted = false;

    // Kill the RX Task first
    // We do this before closing the socket to prevent the task from 
    // trying to read from a closed file descriptor, which causes a crash.
    if (ApStaClassInstance->UdpRxTaskHandle != nullptr)
    {
        vTaskDelete(ApStaClassInstance->UdpRxTaskHandle);
        ApStaClassInstance->UdpRxTaskHandle = nullptr;
    }

    // Close the Socket
    if (ApStaClassInstance->UdpSocket >= 0)
    {
        // shutdown() ensures all pending sends/receives are terminated
        shutdown(ApStaClassInstance->UdpSocket, SHUT_RDWR);
        close(ApStaClassInstance->UdpSocket);
        ApStaClassInstance->UdpSocket = -1;
    }

    if (ApStaClassInstance->IsRuntimeLoggingEnabled)
    {
        ESP_LOGW("UDP", "Mesh UDP System Stopped");
    }

    return true;
}

void AccessPointStation::UdpRxTask(void* pvParameters)
{
    (void)pvParameters;

    uint8_t TempBuffer[1024];
    sockaddr_in SourceAddr;
    socklen_t AddrLen = sizeof(SourceAddr);

    while (true)
    {
        // Safety checks using the generic instance
        if (ApStaClassInstance == nullptr || !ApStaClassInstance->UdpStarted) break;
        if (ApStaClassInstance->UdpSocket < 0) break;

        AddrLen = sizeof(SourceAddr);

        // Block until data arrives or timeout (10ms) occurs
        int BytesReceived = recvfrom(
            ApStaClassInstance->UdpSocket,
            TempBuffer,
            sizeof(TempBuffer),
            0,
            (struct sockaddr*)&SourceAddr,
            &AddrLen
        );
        
        if (BytesReceived > 0)
        {
            // Extract the Sender's IP (Essential for third-party routing logic)
            char SenderIp[16];
            esp_ip4addr_ntoa((const esp_ip4_addr_t*)&SourceAddr.sin_addr.s_addr, SenderIp, sizeof(SenderIp));

            // Thread-safe buffer write
            portENTER_CRITICAL(&ApStaClassInstance->CriticalSection);

            size_t Remaining = sizeof(ApStaClassInstance->RxData) - ApStaClassInstance->LastPositionWritten;
            size_t n = (size_t)BytesReceived;

            if (n <= Remaining)
            {
                // Copy data to the internal buffer
                memcpy(ApStaClassInstance->RxData + ApStaClassInstance->LastPositionWritten, TempBuffer, n);
                ApStaClassInstance->LastPositionWritten += n;

                /* NOTE: Here is where you would call a Callback or 
                   Post to a Queue for your third-party Mesh Class.
                   e.g., MeshRouter.ProcessPacket(TempBuffer, n, SenderIp);
                */
            }

            portEXIT_CRITICAL(&ApStaClassInstance->CriticalSection);

            if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
                ESP_LOGI("UDP_RX", "%d bytes from %s", BytesReceived, SenderIp);
            }
        }

        // Yield to let other system tasks (like WiFi internal maintenance) run
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    vTaskDelete(nullptr);
}

bool AccessPointStation::SetupWifi()
{
    switch (SetupState) 
    {
        case 0: // NVS - Identical to Station
            Error = nvs_flash_init();
            if (Error == ESP_ERR_NVS_NO_FREE_PAGES || Error == ESP_ERR_NVS_NEW_VERSION_FOUND)
            {
                if (nvs_flash_erase() != ESP_OK) return false;
                Error = nvs_flash_init();
            }
            if (Error != ESP_OK) return false;
            SetupState++;
            break;

        case 1: // Netif Core
            if (esp_netif_init() != ESP_OK) return false;
            SetupState++;
            break;

        case 2: // Event loop
            if (esp_event_loop_create_default() != ESP_OK) return false;
            SetupState++;
            break;

        case 3: // Create Dual Interfaces
            ApStaClassInstance->StaNetif = esp_netif_create_default_wifi_sta();
            ApStaClassInstance->ApNetif = esp_netif_create_default_wifi_ap();
            if (ApStaClassInstance->StaNetif == nullptr || ApStaClassInstance->ApNetif == nullptr) return false;
            SetupState++;
            break;

        case 4: // Wi-Fi init
            if (esp_wifi_init(&WifiDriverConfig) != ESP_OK) return false;
            SetupState++;
            break;

        case 5: // Country
            memcpy(WifiCountry.cc, "GB", 2);
            WifiCountry.schan = 1;
            WifiCountry.nchan = 13;
            WifiCountry.policy = WIFI_COUNTRY_POLICY_AUTO;
            if (esp_wifi_set_country(&WifiCountry) != ESP_OK) return false;
            SetupState++;
            break;

        case 6: // Register the 3 Handlers we built
            // 1. Station WiFi Handler
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &AccessPointStation::StaWifiEventHandler, nullptr, nullptr);
            // 2. AP WiFi Handler
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &AccessPointStation::ApWifiEventHandler, nullptr, nullptr);
            // 3. Consolidated IP Handler
            esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                                &AccessPointStation::IpEventHandler, nullptr, nullptr);
            SetupState++;
            break;

        case 7: // Configure AP + STA settings
            memset(&StaWifiServiceConfig, 0, sizeof(wifi_config_t));
            memset(&ApWifiServiceConfig, 0, sizeof(wifi_config_t));

            // STATION: Still uses your router credentials from Kconfig
            strncpy((char*)StaWifiServiceConfig.sta.ssid, CONFIG_ESP_WIFI_SSID, 31);
            strncpy((char*)StaWifiServiceConfig.sta.password, CONFIG_ESP_WIFI_PASSWORD, 63);

            // ACCESS POINT: Dynamic naming
            // This creates a string like "node101" if your UID is 101
            snprintf((char*)ApWifiServiceConfig.ap.ssid, sizeof(ApWifiServiceConfig.ap.ssid), 
                    "node%d", CONFIG_ESP_NODE_UID);

            // Ensure password is set and is at least 8 characters
            strncpy((char*)ApWifiServiceConfig.ap.password, CONFIG_ESP_WIFI_PASSWORD, 63);
            
            ApWifiServiceConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
            ApWifiServiceConfig.ap.channel = 1; 

            SetupState++;
            break;

        case 8: // Set mode to APSTA
            if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) return false;
            SetupState++;
            break;

        case 9: // Apply Configs to specific interfaces
            // Apply STA config to the Station interface
            if (esp_wifi_set_config(WIFI_IF_STA, &StaWifiServiceConfig) != ESP_OK) return false;
            
            // Apply AP config to the Access Point interface
            if (esp_wifi_set_config(WIFI_IF_AP, &ApWifiServiceConfig) != ESP_OK) return false;
            
            SetupState++;
            break;

        case 10: // Start Wi-Fi & Initial Beacon
            if (esp_wifi_start() != ESP_OK) return false;
            
            // Start by advertising "Inifinity" hop until we get an IP
            ApStaClassInstance->UpdateBeaconMetadata(255);
            
            SetupState = 100;
            break;

        case 100:
            SystemInitialized = true;
            break;
    }

    return (SetupState == 100);
}








//==============================================================================//
//                                                                              //
//                                 Factory                                      //
//                                                                              //
//==============================================================================// 

#define FACTORY_TAG "Factory"

Station* WifiFactory::CreateStation(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging)
{
    if (StaClassInstance != nullptr)
    {
        return StaClassInstance;
    }

    if (ApStaClassInstance != nullptr) // Placeholder for access point and ApSta pointers
    {
        return nullptr;
    }

    StaClassInstance = new Station(CoreToUse, UdpPort, EnableRuntimeLogging);

    if (StaClassInstance == nullptr)
    {
        ESP_LOGE(FACTORY_TAG, "Failed to create Station instance!");
        return nullptr;
    }

    ESP_LOGW(FACTORY_TAG, "Station instance created successfully");
    return StaClassInstance;
}

AccessPointStation* WifiFactory::CreateAccessPointStation(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging)
{
    if (ApStaClassInstance != nullptr)
    {
        return ApStaClassInstance;
    }

    if (StaClassInstance != nullptr) // Placeholder for access point and ApSta pointers
    {
        return nullptr;
    }

    ApStaClassInstance = new AccessPointStation(CoreToUse, UdpPort, EnableRuntimeLogging);

    if (ApStaClassInstance == nullptr)
    {
        ESP_LOGE(FACTORY_TAG, "Failed to create AccessPointStation instance!");
        return nullptr;
    }

    ESP_LOGW(FACTORY_TAG, "AccessPointStation instance created successfully");
    return ApStaClassInstance;
}
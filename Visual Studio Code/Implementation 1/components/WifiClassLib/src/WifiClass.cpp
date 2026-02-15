#include "WifiClass.h"
#include "esp_wifi_types_generic.h"
#include "portmacro.h"
#include <cstddef>
#include <cstdint>
#include <string.h>

// Author - Ben Sturdy
// This file implements a class 'Wifi Class'. This class should be instantiated
// only once in a project. This class controls all wireless functionalities.
// This class can set up a system as an Access Point or a Station in WiFi mode. 
// This class can set up and utilise ESP-NOW. The functions with this class can 
// run on the same core as other processes.





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
            break;



        case WIFI_EVENT_SCAN_DONE:
            // This is the trigger for our connection logic
            if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
                ESP_LOGI(STA_TAG, "WiFi Scan Complete. Parsing results...");
            }
            ApStaClassInstance->ParseScanResults();
            ApStaClassInstance->IsScanning = false;
            break;



        case WIFI_EVENT_STA_CONNECTED: 
        {
            wifi_event_sta_connected_t* Event = static_cast<wifi_event_sta_connected_t*>(event_data);
            ApStaClassInstance->IsConnecting = false;
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
            ApStaClassInstance->IsConnecting = false;
            ApStaClassInstance->IsConnectedToParent = false;
            ApStaClassInstance->ApIpAcquired = false;
            
            // Poison the route and wifi data
            ApStaClassInstance->MyHopCount = 255; 
            ApStaClassInstance->ParentDevice.HopCount = 255;
            ApStaClassInstance->IsConnectedToParent = false;
            ApStaClassInstance->IsMasterFound = false;
            ApStaClassInstance->ParentAP.ssid[0] = '\0';
            ApStaClassInstance->UpdateBeaconMetadata(255, ApStaClassInstance->ChildDevices.size());
            
            ApStaClassInstance->StopUdp();
            
            if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
                 wifi_event_sta_disconnected_t* Event = (wifi_event_sta_disconnected_t*)event_data;
                 ESP_LOGE(STA_TAG, "Parent Lost (Reason: %d). System will re-scan soon...", Event->reason);
            }

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
            ApStaClassInstance->UpdateBeaconMetadata(ApStaClassInstance->MyHopCount, 0);

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
            ApStaClassInstance->UpdateBeaconMetadata(255, 0);

            ApStaClassInstance->StopUdp();
            
            if (ApStaClassInstance->IsRuntimeLoggingEnabled) ESP_LOGE(STA_TAG, "STA lost IP");
            break;
        }
    }
}

void AccessPointStation::WifiVendorIeCb(void *ctx, wifi_vendor_ie_type_t type, const uint8_t sa[6], const vendor_ie_data_t *vnd_ie, int rssi) 
{
    const vendor_ie_data_t* data = vnd_ie;

    // 1. Initial check - Is it even a valid pointer?
    if (data == nullptr) return;

    // 2. Debug: See every vendor IE that flies past the radio
    if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
        ESP_LOGI(STA_TAG, "IE Detected from %02x:%02x:%02x:%02x:%02x:%02x | OUI: %02x%02x%02x", 
                 sa[0], sa[1], sa[2], sa[3], sa[4], sa[5],
                 data->vendor_oui[0], data->vendor_oui[1], data->vendor_oui[2]);
    }

    if (data->length < 4) return;

    // 3. Check if the Vendor OUI matches our mesh protocol
    if (data->vendor_oui[0] != MESH_OUI_0 || 
        data->vendor_oui[1] != MESH_OUI_1 || 
        data->vendor_oui[2] != MESH_OUI_2) return;

    // 4. Success Log: We found one of OUR nodes
    if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
        ESP_LOGW(STA_TAG, ">>> MESH IE MATCH! Hop: %d, Children: %d", 
                 data->payload[0], data->payload[1]);
    }

    for (int i = 0; i < 20; i++)
    {
        if (!ApStaClassInstance->CallbackIeData[i].IsValid)
        {
            memcpy(ApStaClassInstance->CallbackIeData[i].MacId, sa, 6);

            ApStaClassInstance->CallbackIeData[i].HopCount = data->payload[0];
            ApStaClassInstance->CallbackIeData[i].ChildCount = data->payload[1];
            ApStaClassInstance->CallbackIeData[i].IsValid = true;

            break;
        }
    }
}

bool AccessPointStation::InitiateMeshScan()
{
    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = false;
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    
    // Non-blocking scan start
    if (esp_wifi_scan_start(&scan_config, false) == ESP_OK)
    {
        IsScanning = true; // Use the bool flag from your header
        return true;
    }
    return false;
}

void AccessPointStation::UpdateBeaconMetadata(uint8_t Hop, uint8_t Children)
{
    // Define the structure exactly as expected by the hardware
    typedef struct {
        vendor_ie_data_t header;
        uint8_t payload[2];
    } __attribute__((packed)) mesh_vendor_ie_t;

    mesh_vendor_ie_t my_ie;
    my_ie.header.element_id = 0xDD;
    my_ie.header.length = 5; // 3 (OUI) + 2 (Payload)
    my_ie.header.vendor_oui[0] = MESH_OUI_0;
    my_ie.header.vendor_oui[1] = MESH_OUI_1;
    my_ie.header.vendor_oui[2] = MESH_OUI_2;
    my_ie.payload[0] = Hop;
    my_ie.payload[1] = Children;

    // IMPORTANT: If we are in the middle of connecting, the driver will return INVALID_ARG.
    // We try to set it, and if it fails, we don't spam the logs, we just wait for the next cyclic call.

    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, nullptr);
    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_1, nullptr);

    esp_err_t res_bcn = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, (vendor_ie_data_t*)&my_ie);
    esp_err_t res_prb = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_1, (vendor_ie_data_t*)&my_ie);

    if (IsRuntimeLoggingEnabled) {
        if (res_bcn == ESP_OK && res_prb == ESP_OK) {
            ESP_LOGI(STA_TAG, "Mesh IE Broadcast Updated: Hop %d", Hop);
        } else {
            // Log the error code specifically to see if it's 0x102 (Invalid Arg) or something else
            ESP_LOGD(STA_TAG, "IE Update Deferred (Driver Busy): 0x%x", res_bcn);
        }
    }
}

void AccessPointStation::ParseScanResults()
{
    uint16_t ApCount = 0;
    uint8_t CurrentBestHop = 255; 
    uint8_t CurrentBestChildren = 255;
    
    Error = esp_wifi_scan_get_ap_num(&ApCount);
    if (ApCount == 0 || Error != ESP_OK) return;

    std::vector<wifi_ap_record_t> ApList(ApCount);
    Error = esp_wifi_scan_get_ap_records(&ApCount, ApList.data());
    if (Error != ESP_OK) return;

    wifi_ap_record_t* TempBestAp = nullptr;
    IsMasterFound = false; 

    for (int i = 0; i < ApCount; i++) 
    {
        wifi_ap_record_t TempRecord = ApList[i];

        // Logging the raw scan result if enabled
        if (IsRuntimeLoggingEnabled) {
            ESP_LOGI(STA_TAG, "[Scan Index %d] SSID: %s | RSSI: %d | Channel: %d", 
                    i, (char*)TempRecord.ssid, TempRecord.rssi, TempRecord.primary);
        }

        // if (strcmp((char*)TempRecord.ssid, "SturdyAP") == 0) 
        // {
        //     TempBestAp = &ApList[i]; 
        //     ParentHopCount = 0;         
        //     IsMasterFound = true;         
        //     if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, ">>> Master (SturdyAP) Found!");
        //     break; 
        // } 
        if (strstr((char*)TempRecord.ssid, "node") != nullptr) 
        {
            // Search our Vendor IE cache for this specific MAC address
            bool foundVendorData = false;
            for (int j = 0; j < 20; j++) 
            {
                if (CallbackIeData[j].IsValid && 
                    memcmp(TempRecord.bssid, CallbackIeData[j].MacId, 6) == 0) 
                {
                    uint8_t nodeHop = CallbackIeData[j].HopCount;
                    uint8_t nodeChildren = CallbackIeData[j].ChildCount;

                    if (nodeHop == 255)
                    {
                        if (IsRuntimeLoggingEnabled) {
                            ESP_LOGW(STA_TAG, "  -- Ignoring node (Hop = 255)");
                        }
                        break;   // skip this AP entirely
                    }

                    foundVendorData = true;

                    if (IsRuntimeLoggingEnabled) {
                        ESP_LOGI(STA_TAG, "  -- Match Found in IE Cache! Hop: %d, Children: %d", 
                                nodeHop, nodeChildren);
                    }

                    if (nodeHop < CurrentBestHop)
                    {
                        TempBestAp = &ApList[i];
                        CurrentBestHop = nodeHop;
                        CurrentBestChildren = nodeChildren;
                    }
                    else if (nodeHop == CurrentBestHop && nodeChildren < CurrentBestChildren)
                    {
                        TempBestAp = &ApList[i];
                        CurrentBestChildren = nodeChildren;
                    }
                    
                    break; 
                }
            }

            if (!foundVendorData && IsRuntimeLoggingEnabled) {
                ESP_LOGD(STA_TAG, "  -- Node found but no matching Vendor IE data in cache.");
            }
        }
    }

    if (TempBestAp != nullptr)
    {
        // Copy the winning record into our persistent class member
        ParentAP = *TempBestAp;

        // Persist the hop info for the chosen parent 
        ParentHopCount = CurrentBestHop;
        ParentDevice.HopCount = CurrentBestHop;

        // Compute hop (guard 255 just in case)
        if (CurrentBestHop == 255) MyHopCount = 255;
        else MyHopCount = (uint8_t)(CurrentBestHop + 1);
    }

    memset(CallbackIeData, 0, sizeof(CallbackIeData));

    IsScanning = false;
}

void AccessPointStation::ConnectToBestAp()
{
    esp_wifi_disconnect();

    wifi_config_t sta_config = {};
    
    // Copy SSID and Password
    if (IsMasterFound) 
    {
        ESP_LOGW(STA_TAG, "ConnectToBestAp: Root Master Found! Connecting to: %s Pass: %s", PARENT_SSID, PARENT_PASS);
        // Use Master (Root) Credentials
        strncpy((char*)sta_config.sta.ssid, PARENT_SSID, sizeof(sta_config.sta.ssid));
        strncpy((char*)sta_config.sta.password, PARENT_PASS, sizeof(sta_config.sta.password));
    }
    else 
    {
        ESP_LOGW(STA_TAG, "ConnectToBestAp: Master NOT found. Connecting to Mesh Parent SSID: %s Pass: %s", (char*)ParentAP.ssid, MY_PASS);
        // Use Mesh Node Credentials
        memcpy(sta_config.sta.ssid, ParentAP.ssid, sizeof(sta_config.sta.ssid));
        strncpy((char*)sta_config.sta.password, MY_PASS, sizeof(sta_config.sta.password));
    }

    // Bind to the specific MAC address (BSSID) we found in scan
    sta_config.sta.bssid_set = true;
    memcpy(sta_config.sta.bssid, ParentAP.bssid, 6);
    
    // Set the channel to the one BestAp is on to speed up connection
    sta_config.sta.channel = ParentAP.primary;

    ESP_LOGW(STA_TAG, "Connection details: BSSID: " MACSTR " | Channel: %d", 
             MAC2STR(sta_config.sta.bssid), sta_config.sta.channel);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));

    esp_wifi_connect();
}

void AccessPointStation::MeshTask(void* pvParameters)
{    
    while (true)
    {
        if (!ApStaClassInstance->IsConnectedToParent) 
        {
            // NEW: Only trigger if we aren't already in the middle of a handshake
            if (ApStaClassInstance->ParentAP.ssid[0] != '\0' && !ApStaClassInstance->IsConnecting)
            {
                ApStaClassInstance->IsConnecting = true; // LOCK the state
                ESP_LOGW(STA_TAG, "Starting stateful connection to %s", ApStaClassInstance->ParentAP.ssid);
                
                ApStaClassInstance->ConnectToBestAp();
            }
            else if (!ApStaClassInstance->IsScanning && !ApStaClassInstance->IsConnecting)
            {
                ApStaClassInstance->InitiateMeshScan();
            }
        }
        
        // Use a 2-second heartbeat for the task
        vTaskDelay(pdMS_TO_TICKS(2000));
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
    while (true)
    {


        vTaskDelay(1);
    }

    vTaskDelete(nullptr);
}

bool AccessPointStation::SetupWifi()
{
    switch (SetupState) 
    {
        case 0: // NVS
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



        case 6: // Register the 3 Handlers
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

            // STATION: master credentials
            strncpy((char*)StaWifiServiceConfig.sta.ssid, PARENT_SSID, 31);
            strncpy((char*)StaWifiServiceConfig.sta.password, PARENT_PASS, 63);

            // Set PMF to capable (standard for WPA2)
            StaWifiServiceConfig.sta.pmf_cfg.capable = true;
            StaWifiServiceConfig.sta.pmf_cfg.required = false;

            ApWifiServiceConfig.ap.pmf_cfg.capable = true;
            ApWifiServiceConfig.ap.pmf_cfg.required = false;

            // ACCESS POINT: Dynamic naming
            snprintf((char*)ApWifiServiceConfig.ap.ssid, sizeof(ApWifiServiceConfig.ap.ssid), 
                    "node%d", CONFIG_ESP_NODE_UID);
                
            ApWifiServiceConfig.ap.ssid_len = strlen((char*)ApWifiServiceConfig.ap.ssid);

            // Ensure password is set and is at least 8 characters
            strncpy((char*)ApWifiServiceConfig.ap.password, MY_PASS, 63);

            ApWifiServiceConfig.ap.max_connection = 4; 
            
            ApWifiServiceConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
            ApWifiServiceConfig.ap.channel = 6; 
            SetupState++;
            break;



        case 8: // Set mode to APSTA
            if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) return false;
            SetupState++;
            break;



        case 9: // Apply Configs to specific interfaces
            if (esp_wifi_set_config(WIFI_IF_STA, &StaWifiServiceConfig) != ESP_OK) return false;
            if (esp_wifi_set_config(WIFI_IF_AP, &ApWifiServiceConfig) != ESP_OK) return false;
            SetupState++;
            break;



        case 10: // Register callbacks for vendor information
        if (esp_wifi_set_vendor_ie_cb(WifiVendorIeCb, this) != ESP_OK) return false;
            SetupState++;
            break;



        case 11: // Start Wi-Fi & Initial Beacon
            if (esp_wifi_start() != ESP_OK) return false;
            
            vTaskDelay(pdMS_TO_TICKS(100));

            // Start by advertising "Inifinity" hop until we get an IP
            ApStaClassInstance->UpdateBeaconMetadata(255, 0);

            // Create the Mesh Management Task
            xTaskCreatePinnedToCore
            (
                &AccessPointStation::MeshTask,   // Function pointer
                "MeshTask",                      // Task name
                4096,                            // Stack size
                this,                            // Pass 'this' as pvParameters
                5,                               // Priority (Medium)
                &MeshTaskHandle,                 // Task handle
                UdpCore                          // Use the core assigned in constructor
            );
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

// Station* WifiFactory::CreateStation(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging)
// {
//     if (StaClassInstance != nullptr)
//     {
//         return StaClassInstance;
//     }

//     if (ApStaClassInstance != nullptr) // Placeholder for access point and ApSta pointers
//     {
//         return nullptr;
//     }

//     StaClassInstance = new Station(CoreToUse, UdpPort, EnableRuntimeLogging);

//     if (StaClassInstance == nullptr)
//     {
//         ESP_LOGE(FACTORY_TAG, "Failed to create Station instance!");
//         return nullptr;
//     }

//     ESP_LOGW(FACTORY_TAG, "Station instance created successfully");
//     return StaClassInstance;
// }

AccessPointStation* WifiFactory::CreateAccessPointStation(uint8_t CoreToUse, uint16_t UdpPort, bool EnableRuntimeLogging)
{
    if (ApStaClassInstance != nullptr)
    {
        return ApStaClassInstance;
    }

    if (false)
    //if (StaClassInstance != nullptr) // Placeholder for access point and ApSta pointers
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
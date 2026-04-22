#include "WifiClass.h"
#include "esp_wifi_types_generic.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "lwip/sockets.h"
#include "lwip/ip4_addr.h"
#include "portmacro.h"
#include <errno.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

static bool IsValidMeshPacket(const uint8_t* Data, int Length)
{
    if (Data == nullptr) return false;
    if (Length < static_cast<int>(PACKET_HEADER_SIZE + sizeof(PACKET_END_DELIMITER))) return false;

    uint16_t StartDelimiter = 0;
    memcpy(&StartDelimiter, Data, sizeof(StartDelimiter));
    if (StartDelimiter != PACKET_START_DELIMITER) return false;

    uint16_t PayloadSizeNetwork = 0;
    memcpy(&PayloadSizeNetwork, Data + 2, sizeof(PayloadSizeNetwork));
    const uint16_t PayloadSize = ntohs(PayloadSizeNetwork);

    const size_t ExpectedLength = PACKET_HEADER_SIZE + static_cast<size_t>(PayloadSize) + sizeof(PACKET_END_DELIMITER);
    if (ExpectedLength != static_cast<size_t>(Length)) return false;

    uint16_t EndDelimiter = 0;
    memcpy(&EndDelimiter, Data + PACKET_HEADER_SIZE + PayloadSize, sizeof(EndDelimiter));
    if (EndDelimiter != PACKET_END_DELIMITER) return false;

    return true;
}

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
    memset(MyApIpAddress, 0, sizeof(MyApIpAddress));
    StateMutex = xSemaphoreCreateMutex();
}



AccessPointStation::~AccessPointStation()
{
    if (StateMutex != nullptr)
    {
        vSemaphoreDelete(StateMutex);
        StateMutex = nullptr;
    }
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
        child.LastHeartbeatUs = 0;
        child.ChildrenCount = 0;
        memcpy(child.MacId, Event->mac, 6);
        child.IpAddress[0] = '\0'; 

        if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
        ApStaClassInstance->ChildDevices.push_back(child);
        const uint8_t ChildCount = static_cast<uint8_t>(ApStaClassInstance->ChildDevices.size());
        const uint8_t MyHop = ApStaClassInstance->MyHopCount;
        if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);

        if (ApStaClassInstance->IsRuntimeLoggingEnabled) 
        {
            ESP_LOGW("MESH_AP", "Child Joined | MAC: " MACSTR " | AID: %d", 
                     MAC2STR(Event->mac), Event->aid);
        }

        ApStaClassInstance->UpdateBeaconMetadata(MyHop, ChildCount);
    } 



    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) 
    {
        wifi_event_ap_stadisconnected_t* Event = (wifi_event_ap_stadisconnected_t*)event_data;
        
        if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
            ESP_LOGE("MESH_AP", "Child Left | MAC: " MACSTR, MAC2STR(Event->mac));
        }

        uint8_t ChildCount = 0;
        uint8_t MyHop = 255;
        if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
        // Precise removal using Erase-Remove Idiom
        auto& list = ApStaClassInstance->ChildDevices;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const WifiDevice& d) {
            return memcmp(d.MacId, Event->mac, 6) == 0;
        }), list.end());
        ChildCount = static_cast<uint8_t>(ApStaClassInstance->ChildDevices.size());
        MyHop = ApStaClassInstance->MyHopCount;
        if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);

        ApStaClassInstance->UpdateBeaconMetadata(MyHop, ChildCount);
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
            ApStaClassInstance->LastHeartbeatUs = 0;
            ApStaClassInstance->LastHeartbeatSentUs = 0;
            ApStaClassInstance->ParentDevice.LastHeartbeatUs = 0;
            ApStaClassInstance->StopUdp();
            break;



        case WIFI_EVENT_SCAN_DONE:
        {
            // This is the trigger for our connection logic
            if (ApStaClassInstance->IsRuntimeLoggingEnabled) {
                ESP_LOGI(STA_TAG, "WiFi Scan Complete. Parsing results...");
            }
            ApStaClassInstance->ParseScanResults();
            ApStaClassInstance->IsScanning = false;

            bool IsConnectedLocal = false;
            bool HasIpLocal = false;
            bool HasCandidateLocal = false;
            uint8_t CurHopLocal = 255;
            uint8_t NewHopLocal = 255;
            if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
            IsConnectedLocal = ApStaClassInstance->IsConnectedToParent;
            HasIpLocal = ApStaClassInstance->ApIpAcquired;
            HasCandidateLocal = ApStaClassInstance->IsCandidateValid;
            CurHopLocal = ApStaClassInstance->ParentDevice.HopCount;
            NewHopLocal = ApStaClassInstance->CandidateHop;
            if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);

            if (IsConnectedLocal && HasIpLocal && HasCandidateLocal)
            {
                if (NewHopLocal < CurHopLocal) 
                {
                    if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
                    ApStaClassInstance->RoamRequested = true;
                    if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);
                    if (ApStaClassInstance->IsRuntimeLoggingEnabled) 
                    {
                        ESP_LOGW(STA_TAG, "Roam requested: current hop %u -> candidate hop %u",
                                CurHopLocal, NewHopLocal);
                    }
                }
            }

            break;
        }



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
            ApStaClassInstance->ParentDevice.LastHeartbeatUs = 0;
            ApStaClassInstance->LastHeartbeatUs = 0;
            ApStaClassInstance->LastHeartbeatSentUs = 0;
            
            // Poison the route and wifi data
            ApStaClassInstance->MyHopCount = 255; 
            ApStaClassInstance->ParentDevice.HopCount = 255;
            ApStaClassInstance->IsConnectedToParent = false;
            ApStaClassInstance->IsMasterFound = false;
            ApStaClassInstance->ParentDevice.WifiRecord.ssid[0] = '\0';
            ApStaClassInstance->DisconnectAllChildren("Parent disconnected");
            
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
            strcpy(ApStaClassInstance->MyStaIpAddress, MyStr);
            strcpy(ApStaClassInstance->ParentDevice.IpAddress, GwStr);

            // char* LastDot = strrchr(ApStaClassInstance->ParentDevice.IpAddress, '.');
            // if (LastDot != nullptr && strcmp(LastDot, ".1") == 0)
            // {
            //     strcpy(LastDot, ".254");
            // }

            // 3. Update State Flags
            ApStaClassInstance->ApIpAcquired = true;
            ApStaClassInstance->IsConnectedToParent = true;
            ApStaClassInstance->LastHeartbeatUs = 0;
            ApStaClassInstance->LastHeartbeatSentUs = 0;
            ApStaClassInstance->ParentDevice.LastHeartbeatUs = 0;   

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
            ApStaClassInstance->UpdateBeaconMetadata(ApStaClassInstance->MyHopCount, (uint8_t)ApStaClassInstance->ChildDevices.size());

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
            if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
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
            if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);

            if (!matched && ApStaClassInstance->IsRuntimeLoggingEnabled) {
                ESP_LOGE("MESH_AP", "Received IP assignment %s but found no matching child!", AssignedIp);
            }
            break;
        }


        
        case IP_EVENT_STA_LOST_IP:
        {
            ApStaClassInstance->ApIpAcquired = false;
            memset(ApStaClassInstance->MyStaIpAddress, 0, 16);
            ApStaClassInstance->LastHeartbeatUs = 0;
            ApStaClassInstance->LastHeartbeatSentUs = 0;
            ApStaClassInstance->ParentDevice.LastHeartbeatUs = 0;

            // MESH LOGIC: Poison the route
            ApStaClassInstance->MyHopCount = 255; 
            ApStaClassInstance->DisconnectAllChildren("Parent IP lost");

            ApStaClassInstance->StopUdp();
            
            if (ApStaClassInstance->IsRuntimeLoggingEnabled) ESP_LOGE(STA_TAG, "STA lost IP");
            break;
        }
    }
}





void AccessPointStation::WifiVendorIeCb(void *ctx, wifi_vendor_ie_type_t type, const uint8_t sa[6], const vendor_ie_data_t *vnd_ie, int rssi) 
{
    const vendor_ie_data_t* data = vnd_ie;

    if (data == nullptr) return;
    if (data->length < 6) return;
    if (data->vendor_oui[0] != MESH_OUI_0 || 
        data->vendor_oui[1] != MESH_OUI_1 || 
        data->vendor_oui[2] != MESH_OUI_2) return;

    if (ApStaClassInstance->IsRuntimeLoggingEnabled) 
    {
        ESP_LOGW(STA_TAG, "IE Detected from %02x:%02x:%02x:%02x:%02x:%02x | OUI: %02x%02x%02x | Hops %d | Children %d", 
                sa[0], sa[1], sa[2], sa[3], sa[4], sa[5],
                data->vendor_oui[0], data->vendor_oui[1], data->vendor_oui[2],
                data->payload[0],
                data->payload[1]);
    }

    for (int i = 0; i < 20; i++)
    {
        if (ApStaClassInstance->CallbackIeData[i].IsValid &&
            memcmp(ApStaClassInstance->CallbackIeData[i].MacId, sa, 6) == 0)
        {
            ApStaClassInstance->CallbackIeData[i].HopCount = data->payload[0];
            ApStaClassInstance->CallbackIeData[i].ChildCount = data->payload[1];
            return;
        }

        if (!ApStaClassInstance->CallbackIeData[i].IsValid)
        {
            memcpy(ApStaClassInstance->CallbackIeData[i].MacId, sa, 6);
            ApStaClassInstance->CallbackIeData[i].HopCount = data->payload[0];
            ApStaClassInstance->CallbackIeData[i].ChildCount = data->payload[1];
            ApStaClassInstance->CallbackIeData[i].IsValid = true;
            return;
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
        IsScanning = true; 
        return true;
    }
    return false;
}

void AccessPointStation::UpdateBeaconMetadata(uint8_t Hop, uint8_t Children)
{
    // Define the structure exactly as expected by the hardware
    typedef struct 
    {
        vendor_ie_data_t header;
        uint8_t payload[2];
    } __attribute__((packed)) mesh_vendor_ie_t;


    mesh_vendor_ie_t my_ie;
    my_ie.header.element_id = 0xDD;
    my_ie.header.length = 6; // 3 (OUI) + 2 (Payload)
    my_ie.header.vendor_oui[0] = MESH_OUI_0;
    my_ie.header.vendor_oui[1] = MESH_OUI_1;
    my_ie.header.vendor_oui[2] = MESH_OUI_2;
    my_ie.header.vendor_oui_type = 0x01;
    my_ie.payload[0] = Hop;
    my_ie.payload[1] = Children;

    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, nullptr);
    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_1, nullptr);

    esp_err_t res_bcn = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, (vendor_ie_data_t*)&my_ie);
    esp_err_t res_prb = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_1, (vendor_ie_data_t*)&my_ie);

    if (IsRuntimeLoggingEnabled) 
    {
        ESP_LOGW(STA_TAG, "UpdateBeaconMetadata(Hop=%u, Children=%u, ActualSize=%u, Len=%u, Type=%u, BCN=0x%x, PRB=0x%x)",
                 Hop, Children, (unsigned)ChildDevices.size(),
                 my_ie.header.length, my_ie.header.vendor_oui_type,
                 res_bcn, res_prb);
    }
}

void AccessPointStation::ParseScanResults()
{
    uint16_t ApCount = 0;
    uint8_t CurrentBestHop = 255; 
    uint8_t CurrentBestChildren = 255;

    // Reset candidate state at the start of each scan parse so stale data
    // cannot influence connection/roaming decisions after a failed scan.
    if (StateMutex != nullptr) xSemaphoreTake(StateMutex, portMAX_DELAY);
    IsCandidateValid = false;
    IsCandidateMaster = false;
    CandidateHop = 0;
    CandidateChildren = 0;
    memset(&CandidateWifiRecord, 0, sizeof(CandidateWifiRecord));
    if (StateMutex != nullptr) xSemaphoreGive(StateMutex);

    
    Error = esp_wifi_scan_get_ap_num(&ApCount);
    if (ApCount == 0 || Error != ESP_OK) return;


    std::vector<wifi_ap_record_t> ApList(ApCount);
    Error = esp_wifi_scan_get_ap_records(&ApCount, ApList.data());
    if (Error != ESP_OK) return;


    wifi_ap_record_t* BestAp = nullptr;
    bool MasterFound = false; 


    for (int i = 0; i < ApCount; i++) 
    {
        if (IsRuntimeLoggingEnabled) 
        {
            ESP_LOGW(STA_TAG, "Scan Index [%d] SSID: %s | RSSI: %d | Channel: %d", 
                    i, (char*)ApList[i].ssid, ApList[i].rssi, ApList[i].primary);
        }


        if (ENABLE_MASTER_CONNECTION == true && strcmp((char*)ApList[i].ssid, "SturdyAP") == 0) 
        {
            BestAp = &ApList[i];   
            CurrentBestHop = 0;
            CurrentBestChildren = 0; 
            MasterFound = true;         
            if (IsRuntimeLoggingEnabled) ESP_LOGW(STA_TAG, ">>> Master (SturdyAP) Found!");
            break; 
        } 


        else if (strstr((char*)ApList[i].ssid, "node") != nullptr) 
        {
            if (IsRuntimeLoggingEnabled)
            {
                ESP_LOGW(STA_TAG, "Node AP BSSID = " MACSTR, MAC2STR(ApList[i].bssid));
            }
            bool foundVendorData = false;
            
            for (int j = 0; j < 20; j++) 
            {
                if (IsRuntimeLoggingEnabled)
                {
                    ESP_LOGW(STA_TAG, "IE Cache [%d] Valid=%d MAC=" MACSTR " Hop=%u Child=%u",
                    j,
                    CallbackIeData[j].IsValid,
                    MAC2STR(CallbackIeData[j].MacId),
                    CallbackIeData[j].HopCount,
                    CallbackIeData[j].ChildCount);
                }

                // if wifi record matches IE scan by BSSID
                if (CallbackIeData[j].IsValid && 
                    memcmp(ApList[i].bssid, CallbackIeData[j].MacId, 6) == 0) 
                {

                    // Hop count unset, device leads nowhere
                    if (CallbackIeData[j].HopCount == 255)
                    {
                        if (IsRuntimeLoggingEnabled) 
                        {
                            ESP_LOGW(STA_TAG, "  -- Ignoring node (Hop = 255)");
                        }
                        break;
                    }


                    // Max connections on device already
                    if (CallbackIeData[j].ChildCount >= MAX_STA_CONN) 
                    {
                        if (IsRuntimeLoggingEnabled) 
                        {
                            ESP_LOGW(STA_TAG, "  -- Ignoring node (Full Children: %d/%d)",
                                    CallbackIeData[j].ChildCount, MAX_STA_CONN);
                        }
                        break;
                    }


                    foundVendorData = true;
                    if (IsRuntimeLoggingEnabled) 
                    {
                        ESP_LOGW(STA_TAG, "  -- Match Found in IE Cache! Hop: %d, Children: %d", 
                                CallbackIeData[j].HopCount, CallbackIeData[j].ChildCount);
                    }


                    // Better hops
                    if (CallbackIeData[j].HopCount < CurrentBestHop)
                    {
                        BestAp = &ApList[i];
                        CurrentBestHop = CallbackIeData[j].HopCount;
                        CurrentBestChildren = CallbackIeData[j].ChildCount;

                        if (IsRuntimeLoggingEnabled) 
                        {
                            ESP_LOGW(STA_TAG, "  -- New Best Match, Better Hop Count");
                        }
                    }


                    // Same hops, less children
                    else if (CallbackIeData[j].HopCount == CurrentBestHop 
                        && CallbackIeData[j].ChildCount < CurrentBestChildren)
                    {
                        BestAp = &ApList[i];
                        CurrentBestChildren = CallbackIeData[j].ChildCount;

                        if (IsRuntimeLoggingEnabled) 
                        {
                            ESP_LOGW(STA_TAG, "  -- New Best Match, Better Children Count");
                        }
                    }
                    
                    break; 
                }
            }

            if (!foundVendorData && IsRuntimeLoggingEnabled) 
            {
                ESP_LOGE(STA_TAG, "  -- Node found but no good Vendor IE data in cache.");
            }
        }
    }


    if (BestAp != nullptr)
    {
        bool CanApplyParentRoute = false;
        if (StateMutex != nullptr) xSemaphoreTake(StateMutex, portMAX_DELAY);
        CanApplyParentRoute = (!IsConnectedToParent && !IsConnecting);
        IsCandidateValid = true;
        IsCandidateMaster = MasterFound;
        CandidateWifiRecord = *BestAp;
        CandidateHop = CurrentBestHop;
        CandidateChildren = CurrentBestChildren;

        if (CanApplyParentRoute)
        {
            IsMasterFound = IsCandidateMaster;
            ParentDevice.WifiRecord = CandidateWifiRecord;
            ParentDevice.HopCount = CandidateHop;
            ParentDevice.ChildrenCount = CandidateChildren;

            MyHopCount = (CandidateHop == 255) ? 255 : (uint8_t)(CandidateHop + 1);
        }
        if (StateMutex != nullptr) xSemaphoreGive(StateMutex);
    }

    else
    {
        if (StateMutex != nullptr) xSemaphoreTake(StateMutex, portMAX_DELAY);
        IsCandidateValid = false;
        if (StateMutex != nullptr) xSemaphoreGive(StateMutex);
    }

    memset(CallbackIeData, 0, sizeof(CallbackIeData));

    IsScanning = false;
}

void AccessPointStation::ConnectToBestAp()
{
    esp_wifi_disconnect();
    wifi_config_t sta_config = {};
    

    if (IsMasterFound) 
    {
        ESP_LOGW(STA_TAG, "ConnectToBestAp: Root Master Found! Connecting to: %s | Pass: %s", PARENT_SSID, PARENT_PASS);
        strncpy((char*)sta_config.sta.ssid, PARENT_SSID, sizeof(sta_config.sta.ssid));
        strncpy((char*)sta_config.sta.password, PARENT_PASS, sizeof(sta_config.sta.password));
    }


    else 
    {
        ESP_LOGW(STA_TAG, "ConnectToBestAp: Master NOT found. Connecting to Mesh Parent SSID: %s | Pass: %s", (char*)ParentDevice.WifiRecord.ssid, MY_PASS);
        memcpy(sta_config.sta.ssid, ParentDevice.WifiRecord.ssid, sizeof(sta_config.sta.ssid));
        strncpy((char*)sta_config.sta.password, MY_PASS, sizeof(sta_config.sta.password));
    }


    sta_config.sta.bssid_set = true;
    memcpy(sta_config.sta.bssid, ParentDevice.WifiRecord.bssid, 6);
    sta_config.sta.channel = ParentDevice.WifiRecord.primary;


    ESP_LOGW(STA_TAG, "Connection details: BSSID: " MACSTR " | Channel: %d", 
             MAC2STR(sta_config.sta.bssid), sta_config.sta.channel);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));


    esp_wifi_connect();
}

void AccessPointStation::MeshTask(void* pvParameters)
{
    uint8_t Counter = 1;
    
    while (true)
    {
        // 10 second counter, increments of 100ms
        vTaskDelay(pdMS_TO_TICKS(100));
        Counter ++;
        if (Counter >= 101) Counter = 1;

        ApStaClassInstance->CheckParentHeartbeatTimeout();
        ApStaClassInstance->CheckChildHeartbeatTimeouts();


        // 5s
        if (Counter % 50 == 0) 
        {                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        
            ApStaClassInstance->UpdateBeaconMetadata(
                ApStaClassInstance->MyHopCount,
                (uint8_t)ApStaClassInstance->ChildDevices.size()
            );
        }


        // 2s
        if (Counter % 20 == 0)
        {
            if (!ApStaClassInstance->IsConnectedToParent &&
                !ApStaClassInstance->IsScanning &&
                !ApStaClassInstance->IsConnecting)
            {
                ApStaClassInstance->InitiateMeshScan();
            }

            
            if (!ApStaClassInstance->IsConnectedToParent &&
                !ApStaClassInstance->IsConnecting &&
                ApStaClassInstance->ParentDevice.WifiRecord.ssid[0] != '\0')
            {
                ApStaClassInstance->IsConnecting = true;
                ApStaClassInstance->ConnectToBestAp();
            }

            if (ApStaClassInstance->IsConnectedToParent &&
                !ApStaClassInstance->IsConnecting &&
                ApStaClassInstance->RoamRequested &&
                ApStaClassInstance->IsCandidateValid)
            {
                bool ShouldRoamNow = false;
                wifi_ap_record_t RoamWifiRecord{};
                uint8_t RoamHop = 255;

                if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
                if (ApStaClassInstance->IsConnectedToParent &&
                    !ApStaClassInstance->IsConnecting &&
                    ApStaClassInstance->RoamRequested &&
                    ApStaClassInstance->IsCandidateValid)
                {
                    ApStaClassInstance->RoamRequested = false;
                    ApStaClassInstance->IsConnecting = true;

                    ApStaClassInstance->IsMasterFound = ApStaClassInstance->IsCandidateMaster;
                    ApStaClassInstance->ParentDevice.WifiRecord = ApStaClassInstance->CandidateWifiRecord;
                    ApStaClassInstance->ParentDevice.HopCount = ApStaClassInstance->CandidateHop;
                    ApStaClassInstance->MyHopCount =
                        (ApStaClassInstance->CandidateHop == 255) ? 255 : (uint8_t)(ApStaClassInstance->CandidateHop + 1);

                    RoamWifiRecord = ApStaClassInstance->ParentDevice.WifiRecord;
                    RoamHop = ApStaClassInstance->ParentDevice.HopCount;
                    ShouldRoamNow = true;
                }
                if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);

                if (ShouldRoamNow)
                {
                    ESP_LOGW(STA_TAG, "Roaming now to %s (hop %u)",
                            (char*)RoamWifiRecord.ssid,
                            RoamHop);

                    esp_wifi_disconnect();
                    vTaskDelay(pdMS_TO_TICKS(200));
                    ApStaClassInstance->ConnectToBestAp();
                }
            }
        }
    }

    vTaskDelete(NULL);
}

void AccessPointStation::CheckParentHeartbeatTimeout()
{
    static uint64_t LastHeartbeatDiagUs = 0;

    bool IsConnectedToParentLocal = false;
    bool ApIpAcquiredLocal = false;
    bool IsMasterFoundLocal = false;
    uint8_t MyHopCountLocal = 255;
    char MyStaIpAddressLocal[16]{};
    char ParentIpLocal[16]{};
    int64_t LastHeartbeatUsLocal = 0;
    int64_t LastHeartbeatSentUsLocal = 0;

    if (StateMutex != nullptr) xSemaphoreTake(StateMutex, portMAX_DELAY);
    IsConnectedToParentLocal = IsConnectedToParent;
    ApIpAcquiredLocal = ApIpAcquired;
    IsMasterFoundLocal = IsMasterFound;
    MyHopCountLocal = MyHopCount;
    strncpy(MyStaIpAddressLocal, MyStaIpAddress, sizeof(MyStaIpAddressLocal) - 1);
    MyStaIpAddressLocal[sizeof(MyStaIpAddressLocal) - 1] = '\0';
    strncpy(ParentIpLocal, ParentDevice.IpAddress, sizeof(ParentIpLocal) - 1);
    ParentIpLocal[sizeof(ParentIpLocal) - 1] = '\0';
    LastHeartbeatUsLocal = LastHeartbeatUs;
    LastHeartbeatSentUsLocal = LastHeartbeatSentUs;
    if (StateMutex != nullptr) xSemaphoreGive(StateMutex);

    if (!IsConnectedToParentLocal) return;
    if (!ApIpAcquiredLocal) return;
    if (MyStaIpAddressLocal[0] == '\0') return;
    if (ParentIpLocal[0] == '\0') return;
    if (IsMasterFoundLocal && MyHopCountLocal == 1) return;
    if (strcmp(ParentIpLocal, "192.168.0.254") == 0) return;

    const uint64_t TimeNow = esp_timer_get_time();

    if (IsRuntimeLoggingEnabled && (TimeNow - LastHeartbeatDiagUs >= 5000000))
    {
        const uint64_t RxAgeUs = (LastHeartbeatUsLocal > 0) ? (TimeNow - static_cast<uint64_t>(LastHeartbeatUsLocal)) : 0;
        const uint64_t TxAgeUs = (LastHeartbeatSentUsLocal > 0) ? (TimeNow - static_cast<uint64_t>(LastHeartbeatSentUsLocal)) : 0;
        ESP_LOGW(STA_TAG, "HB diag | parent=%s | rx_age_us=%llu | tx_age_us=%llu | timeout_us=%llu",
                 ParentIpLocal,
                 RxAgeUs,
                 TxAgeUs,
                 static_cast<unsigned long long>(HEARTBEAT_TIMEOUT_US));
        LastHeartbeatDiagUs = TimeNow;
    }

    uint64_t ReferenceHeartbeatUs = 0;
    if (LastHeartbeatUsLocal != 0)
    {
        ReferenceHeartbeatUs = static_cast<uint64_t>(LastHeartbeatUsLocal);
    }
    else if (LastHeartbeatSentUsLocal != 0)
    {
        // No reply received yet; supervise from the most recent request we sent.
        ReferenceHeartbeatUs = static_cast<uint64_t>(LastHeartbeatSentUsLocal);
    }
    else
    {
        return;
    }

    const uint64_t TimeSinceLastHeartbeat = TimeNow - ReferenceHeartbeatUs;

    if (TimeSinceLastHeartbeat <= HEARTBEAT_TIMEOUT_US) return;

    if (IsRuntimeLoggingEnabled)
    {
        ESP_LOGE(STA_TAG, "Parent heartbeat timeout! Last valid heartbeat/ref was %llu us ago. Initiating disconnect.", TimeSinceLastHeartbeat);
    }

    if (StateMutex != nullptr) xSemaphoreTake(StateMutex, portMAX_DELAY);
    LastHeartbeatUs = 0;
    LastHeartbeatSentUs = 0;
    ParentDevice.LastHeartbeatUs = 0;
    if (StateMutex != nullptr) xSemaphoreGive(StateMutex);
    esp_wifi_disconnect();
}

void AccessPointStation::CheckChildHeartbeatTimeouts()
{
    const uint64_t TimeNow = esp_timer_get_time();
    bool ChildRemoved = false;
    uint8_t ChildCountAfter = 0;
    uint8_t MyHop = 255;

    while (true)
    {
        uint16_t TimedOutAid = 0;
        uint64_t TimeSinceLastHeartbeat = 0;
        uint8_t TimedOutMac[6]{};
        char TimedOutIp[16]{};
        bool TimedOutBeforeFirstHeartbeat = false;

        if (StateMutex != nullptr) xSemaphoreTake(StateMutex, portMAX_DELAY);
        for (size_t i = 0; i < ChildDevices.size(); i++)
        {
            WifiDevice& Child = ChildDevices[i];

            const bool HasValidWifiData = (Child.aid != 0);
            if (!HasValidWifiData) continue;

            if (Child.LastHeartbeatUs == 0)
            {
                TimeSinceLastHeartbeat = TimeNow - Child.TimeOfConnection;
                if (TimeSinceLastHeartbeat <= CHILD_FIRST_HEARTBEAT_GRACE_US) continue;
                TimedOutBeforeFirstHeartbeat = true;
            }
            else
            {
                TimeSinceLastHeartbeat = TimeNow - Child.LastHeartbeatUs;
                if (TimeSinceLastHeartbeat <= HEARTBEAT_TIMEOUT_US) continue;
            }

            TimedOutAid = Child.aid;
            memcpy(TimedOutMac, Child.MacId, sizeof(TimedOutMac));
            strncpy(TimedOutIp, Child.IpAddress, sizeof(TimedOutIp) - 1);
            TimedOutIp[sizeof(TimedOutIp) - 1] = '\0';

            ChildDevices.erase(ChildDevices.begin() + static_cast<long>(i));
            ChildRemoved = true;
            break;
        }

        ChildCountAfter = static_cast<uint8_t>(ChildDevices.size());
        MyHop = MyHopCount;
        if (StateMutex != nullptr) xSemaphoreGive(StateMutex);

        if (TimedOutAid == 0) break;

        if (IsRuntimeLoggingEnabled)
        {
            if (TimedOutBeforeFirstHeartbeat)
            {
                ESP_LOGE("MESH_AP", "Child timeout before first heartbeat | MAC: " MACSTR " | IP: %s | AID: %u | Connected %llu us ago",
                        MAC2STR(TimedOutMac), TimedOutIp, TimedOutAid, TimeSinceLastHeartbeat);
            }
            else
            {
                ESP_LOGE("MESH_AP", "Child heartbeat timeout | MAC: " MACSTR " | IP: %s | AID: %u | Last heartbeat %llu us ago",
                        MAC2STR(TimedOutMac), TimedOutIp, TimedOutAid, TimeSinceLastHeartbeat);
            }
        }

        // Timeout for one child should only remove that specific child.
        esp_wifi_deauth_sta(TimedOutAid);
    }

    if (ChildRemoved) UpdateBeaconMetadata(MyHop, ChildCountAfter);
}

void AccessPointStation::DisconnectAllChildren(const char* Reason)
{
    std::vector<WifiDevice> Snapshot;
    uint8_t MyHop = 255;

    if (StateMutex != nullptr) xSemaphoreTake(StateMutex, portMAX_DELAY);
    Snapshot = ChildDevices;
    ChildDevices.clear();
    MyHop = MyHopCount;
    if (StateMutex != nullptr) xSemaphoreGive(StateMutex);

    for (const WifiDevice& Child : Snapshot)
    {
        if (Child.aid != 0) esp_wifi_deauth_sta(Child.aid);

        if (IsRuntimeLoggingEnabled)
        {
            ESP_LOGW("MESH_AP", "Deauth child (cascade) | MAC: " MACSTR " | IP: %s | Reason: %s",
                     MAC2STR(Child.MacId), Child.IpAddress, (Reason != nullptr ? Reason : "unspecified"));
        }
    }

    UpdateBeaconMetadata(MyHop, 0);
}




void AccessPointStation::HandleReceivedData(uint8_t* Data, int Length, WifiDevice* SourceDevice, bool IsFromParent)
{
    if (!Data) return;
    if (Length <= 48 + 2) return;
    if (!IsValidMeshPacket(Data, Length)) return;

    // Learn child UID from first valid packet seen from that child.
    // PacketHeader::slaveUid is bytes [8..15] in the packed 48-byte header.
    if (!IsFromParent && SourceDevice != nullptr && SourceDevice->UID == 0)
    {
        uint64_t LearnedUid = 0;
        memcpy(&LearnedUid, Data + 8, sizeof(LearnedUid));

        if (LearnedUid != 0)
        {
            SourceDevice->UID = LearnedUid;

            if (IsRuntimeLoggingEnabled)
            {
                ESP_LOGI("MESH_UID", "Learned child UID %llu for child IP %s",
                         (unsigned long long)SourceDevice->UID, SourceDevice->IpAddress);
            }
        }
    }

    uint8_t PacketType = Data[37];
    const uint64_t TimeNow = esp_timer_get_time();

    switch (PacketType)
    {
        case 79:    // Heartbeat
            if (IsFromParent)
            {
                uint64_t SenderUid = 0;
                memcpy(&SenderUid, Data + 8, sizeof(SenderUid));

                // In AP+STA mode, source IP can collide (e.g., both appear as
                // 192.168.4.1). Ignore heartbeats stamped with our own UID so
                // local looped packets cannot masquerade as parent replies.
                if (SenderUid != MY_UID)
                {
                    LastHeartbeatUs = TimeNow;
                    ParentDevice.LastHeartbeatUs = TimeNow;
                    if (IsRuntimeLoggingEnabled)
                    {
                        ESP_LOGI(STA_TAG, "Heartbeat reply received from parent (sender UID=%llu)",
                                 static_cast<unsigned long long>(SenderUid));
                    }
                }
                else if (IsRuntimeLoggingEnabled)
                {
                    ESP_LOGW(STA_TAG, "Ignored heartbeat with local UID on parent path (UID=%llu)",
                             static_cast<unsigned long long>(SenderUid));
                }
            }
            else if (SourceDevice != nullptr)
            {
                SourceDevice->LastHeartbeatUs = TimeNow;
                if (IsRuntimeLoggingEnabled)
                {
                    ESP_LOGI("MESH_AP", "Heartbeat received from child IP %s", SourceDevice->IpAddress);
                }
            }
            break;

        default:
            break;
    }
}

size_t AccessPointStation::GenerateResponsePayload(const uint8_t* ReceivedPacketData, int ReceivedLength, bool ShouldReply, uint8_t* ResponsePayloadBuffer, size_t ResponseBufferSize)
{

    if (ResponsePayloadBuffer != nullptr && ResponseBufferSize > 0) memset(ResponsePayloadBuffer, 0, ResponseBufferSize);
    if (!ReceivedPacketData) return 0;
    if (!ResponsePayloadBuffer) return 0;
    if (ReceivedLength < 48 + 2) return 0;


    uint8_t Data;
    size_t PayloadLength = 0;
    uint8_t PacketType = ReceivedPacketData[37];

    switch (PacketType)
    {
        case 0:     // No data
            PayloadLength = 0;
            break;


        case 79:    // Heartbeat
            if (ShouldReply)
            {
                Data = 79;
                PayloadLength = sizeof(Data);
            }
            else
            {
                PayloadLength = 0;
            }
            break;


        default:
            return 0;
            break;
    }

    if (PayloadLength == 0) return 0;
    if (ResponseBufferSize < PayloadLength) return 0;

    memcpy(ResponsePayloadBuffer, &Data, PayloadLength);
    return PayloadLength;
}


size_t AccessPointStation::CreatePacket(const uint8_t* DataToInclude,
                    size_t DataLength,
                    uint8_t PacketType,
                    uint8_t* PacketOut,
                    size_t OutputBufferSize)
{
    if (!DataToInclude) return 0;
    if (!PacketOut) return 0;
    if (DataLength > 65535) return 0;
    if (PacketType == 0) return 0;
    if (OutputBufferSize < DataLength + 48 + 2) return 0;

    PacketHeader TempHeader{};

    TempHeader.startDelimiter = PACKET_START_DELIMITER;
    TempHeader.payloadSize = htons(static_cast<uint16_t>(DataLength));
    TempHeader.slaveUid = MY_UID;
    TempHeader.senderTimestampUs = (uint64_t)esp_timer_get_time();
    TempHeader.prevCycleTimeUs = 0;
    TempHeader.chainedSlaveCount = ChildDevices.size();
    TempHeader.PacketType = PacketType;
    TempHeader.flags = 0;
    TempHeader.headerVersion = 1;
    TempHeader.networkId = 0;
    TempHeader.chainDistance = MyHopCount;
    TempHeader.ttl = 10;
    if (TempHeader.ttl == 0)
    {
        TempHeader.ttl = 10;
    }
    TempHeader.crc32 = 0;

    uint8_t* p = PacketOut;

    memcpy(p, &TempHeader, sizeof(PacketHeader));
    memcpy(p + sizeof(PacketHeader), DataToInclude, DataLength);
    memcpy(p + sizeof(PacketHeader) + DataLength, &PACKET_END_DELIMITER, 2);

    return PACKET_HEADER_SIZE + DataLength + sizeof(PACKET_END_DELIMITER);
}

bool AccessPointStation::TryAddForwarding(const uint8_t* DataIn, int LengthIn, uint8_t* DataOut, size_t& LengthOut)
{
    if (!DataIn || !DataOut) return false;
    if (!IsValidMeshPacket(DataIn, LengthIn))
    {
        LengthOut = 0;
        return false;
    }

    const uint8_t ForwardMode  = DataIn[43];
    const uint8_t TtlIn = DataIn[42];

    if (ForwardMode == 0)
    {
        memcpy(DataOut, DataIn, LengthIn);
        LengthOut = LengthIn;
        return false;
    }

    if (ForwardMode == 1)
    {
        if (TtlIn == 0) return false;

        memcpy(DataOut, DataIn, LengthIn);
        DataOut[42] = static_cast<uint8_t>(TtlIn - 1); // Decrement TTL per hop
        LengthOut = static_cast<size_t>(LengthIn);
        return true;
    }

    if (ForwardMode == 2)
    {
        if (TtlIn == 0) return false;

        memcpy(DataOut, DataIn, LengthIn);
        DataOut[42] = static_cast<uint8_t>(TtlIn - 1); // Decrement TTL per hop
        LengthOut = static_cast<size_t>(LengthIn);
        return true;
    }

    return false;
}

bool AccessPointStation::DetermineDestinationAddress(const sockaddr_in& SourceAddress, const uint8_t* Data, int DataLength, sockaddr_in& DestinationAddress)
{

    if (!Data) return 0;
    if (DataLength < 48 + 2) return 0;

    const uint8_t ForwardMode = Data[43];

    switch (ForwardMode)
    {
        case 0: // Return to sender
            DestinationAddress = SourceAddress;
            return true;
            break;



        case 1: // Downstream
        {
            const uint8_t* DestinationUid = Data + 16;
            char ChildIp[16]{};

            bool Found = false;
            if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
            for (size_t i = 0; i < ApStaClassInstance->ChildDevices.size(); i++)
            {
                // Compare 8-byte UID
                if (memcmp(&ApStaClassInstance->ChildDevices[i].UID, DestinationUid, 8) == 0)
                {
                    strncpy(ChildIp, ApStaClassInstance->ChildDevices[i].IpAddress, sizeof(ChildIp) - 1);
                    ChildIp[sizeof(ChildIp) - 1] = '\0';
                    Found = true;
                    break;
                }
            }
            if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);

            if (!Found) return false;

            sockaddr_in Destination{};
            Destination.sin_family = AF_INET;
            Destination.sin_port   = htons(ApStaClassInstance->UdpPort);

            if (inet_pton(AF_INET,
              ChildIp,
              &Destination.sin_addr) != 1)
            {
                return false;
            }

            DestinationAddress = Destination;

            return true;
            break;
        }
        


        case 2: // Upstream
        {
            sockaddr_in Destination{};
            Destination.sin_family = AF_INET;
            Destination.sin_port   = htons(ApStaClassInstance->UdpPort);

            bool IsMasterFoundLocal = false;
            char ParentIp[16]{};
            if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
            IsMasterFoundLocal = IsMasterFound;
            strncpy(ParentIp, ApStaClassInstance->ParentDevice.IpAddress, sizeof(ParentIp) - 1);
            ParentIp[sizeof(ParentIp) - 1] = '\0';
            if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);

            if (IsMasterFoundLocal)
            {
                if (inet_pton(AF_INET, "192.168.0.254", &Destination.sin_addr) != 1)
                {
                    return false;
                }
            }
            else 
            {
                if (inet_pton(AF_INET,
                  ParentIp,
                  &Destination.sin_addr) != 1)
                {
                    return false;
                }
            }

            DestinationAddress = Destination;

            return true;
            break;
        }



        default:
            return false;
            break;
    }
}

size_t AccessPointStation::SendData(const uint8_t* Data, int Length, const sockaddr_in& DestinationAddress)
{
    if (!Data) return 0;
    if (Length <= 0) return 0;
    if (ApStaClassInstance->UdpSocket < 0) return 0;

    int SentBytes = sendto(ApStaClassInstance->UdpSocket,
                           Data,
                           Length,
                           0,
                           (const sockaddr*)&DestinationAddress,
                           sizeof(DestinationAddress));

    if (SentBytes < 0)
    {
        return 0;
    }

    return static_cast<size_t>(SentBytes);
}

WifiDevice* AccessPointStation::FindDeviceByIp(const sockaddr_in& SourceAddress, bool* IsParent)
{
    char SourceIp[16]{};

    if (IsParent != nullptr) *IsParent = false;
    if (inet_ntop(AF_INET, &SourceAddress.sin_addr, SourceIp, sizeof(SourceIp)) == nullptr) return nullptr;

    if (strcmp(SourceIp, ParentDevice.IpAddress) == 0)
    {
        if (IsParent != nullptr) *IsParent = true;
        return &ParentDevice;
    }

    // Gateway-node special case: replies from Beckhoff master (.254) should
    // be treated as upstream-parent replies for heartbeat supervision.
    if (IsMasterFound && strcmp(SourceIp, "192.168.0.254") == 0)
    {
        if (IsParent != nullptr) *IsParent = true;
        return &ParentDevice;
    }

    for (size_t i = 0; i < ChildDevices.size(); i++)
    {
        if (strcmp(SourceIp, ChildDevices[i].IpAddress) == 0) return &ChildDevices[i];
    }

    return nullptr;
}



void AccessPointStation::ReceiveTask(void* pvParameters)
{
    static uint64_t LastRecvErrorLogUs = 0;
    uint8_t ReceiveBuffer[800];
    int ReceivedBytes = 0;
    uint8_t TempBuffer1[800];
    uint8_t TempBuffer2[800];
    uint8_t SendBuffer[800];
    size_t SendingBytes = 0;
    size_t ForwardLength = 0;

    while(true)
    {
        sockaddr_in SourceAddress{}, DestinationAddress{};
        socklen_t AddressLength = sizeof(SourceAddress);

        ReceivedBytes = recvfrom(ApStaClassInstance->UdpSocket,
                                ReceiveBuffer,
                                sizeof(ReceiveBuffer),
                                0,
                                (sockaddr*)&SourceAddress,
                                &AddressLength);

        if (ReceivedBytes > 0)
        {
            bool IsFromParent = false;
            bool ShouldReply = false;
            bool IsForwardPacket = false;
            bool IsHeartbeatPacket = false;
            uint64_t SenderUid = 0;
            if (IsValidMeshPacket(ReceiveBuffer, ReceivedBytes))
            {
                // Any packet marked for forwarding is transit traffic and must not
                // enter local payload/reply processing, even if forwarding fails.
                IsForwardPacket = (ReceiveBuffer[43] != 0);
                IsHeartbeatPacket = (ReceiveBuffer[37] == 79);
                memcpy(&SenderUid, ReceiveBuffer + 8, sizeof(SenderUid));
            }

            // Drop packets that originate from this node's own UID. In AP+STA
            // chains, local routing can loop traffic back into the RX socket.
            if (SenderUid == MY_UID)
            {
                if (ApStaClassInstance->IsRuntimeLoggingEnabled)
                {
                    ESP_LOGW(STA_TAG, "Dropped self-origin packet in RX path (UID=%llu)",
                             static_cast<unsigned long long>(SenderUid));
                }
                continue;
            }
            if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
            WifiDevice* FoundDevice = ApStaClassInstance->FindDeviceByIp(SourceAddress, &IsFromParent);
            ApStaClassInstance->HandleReceivedData(ReceiveBuffer, ReceivedBytes, FoundDevice, IsFromParent);
            ShouldReply = (!IsFromParent && FoundDevice != nullptr);
            if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);

            if (ApStaClassInstance->IsRuntimeLoggingEnabled && IsHeartbeatPacket)
            {
                ESP_LOGI(STA_TAG, "Heartbeat RX | senderUID=%llu | fromParent=%d | shouldReply=%d | isForward=%d",
                         static_cast<unsigned long long>(SenderUid),
                         IsFromParent ? 1 : 0,
                         ShouldReply ? 1 : 0,
                         IsForwardPacket ? 1 : 0);
            }

            // Forward packets that are explicitly marked for mesh forwarding.
            ForwardLength = 0;
            if (ApStaClassInstance->TryAddForwarding(ReceiveBuffer, ReceivedBytes, SendBuffer, ForwardLength))
            {
                if (ForwardLength > 0 &&
                    ApStaClassInstance->DetermineDestinationAddress(SourceAddress, SendBuffer, static_cast<int>(ForwardLength), DestinationAddress))
                {
                    ApStaClassInstance->SendData(SendBuffer, static_cast<int>(ForwardLength), DestinationAddress);
                }
            }

            // Forwarded packets are transit traffic, not for local processing.
            if (IsForwardPacket) continue;

            SendingBytes = ApStaClassInstance->GenerateResponsePayload(
                ReceiveBuffer,
                ReceivedBytes,
                ShouldReply,
                TempBuffer1,
                sizeof(TempBuffer1));
            if (SendingBytes <= 0) continue;

            SendingBytes = ApStaClassInstance->CreatePacket(TempBuffer1, SendingBytes, 79, TempBuffer2, sizeof(TempBuffer2));
            if (SendingBytes <= 0) continue;

            memcpy(SendBuffer, TempBuffer2, SendingBytes);

            if (!ApStaClassInstance->DetermineDestinationAddress(SourceAddress, SendBuffer, static_cast<int>(SendingBytes), DestinationAddress)) continue;

            ApStaClassInstance->SendData(SendBuffer, static_cast<int>(SendingBytes), DestinationAddress);
        }
        else if (ReceivedBytes < 0)
        {
            const int RecvErrno = errno;
            if (RecvErrno != EWOULDBLOCK && RecvErrno != EAGAIN)
            {
                const uint64_t TimeNow = esp_timer_get_time();
                if (ApStaClassInstance->IsRuntimeLoggingEnabled &&
                    (TimeNow - LastRecvErrorLogUs >= 5000000))
                {
                    ESP_LOGW("UDP", "recvfrom error: errno=%d", RecvErrno);
                    LastRecvErrorLogUs = TimeNow;
                }
            }
        }

        vTaskDelay(1);
    }

    vTaskDelete(nullptr);
}



void AccessPointStation::TransmitTask(void* pvParameters)
{
    uint64_t CurrentTimeUs = 0;
    uint64_t LastHeartbeatTimeUs = 0;
    uint64_t LastSystemInfoTimeUs = 0;
    uint64_t LastTxDiagUs = 0;
    uint32_t HeartbeatAttemptCount = 0;
    uint32_t HeartbeatSentCount = 0;
    uint32_t HeartbeatSendFailCount = 0;
    uint32_t SystemInfoSentCount = 0;

    struct SystemInfoPayload
    {
        uint64_t uptimeUs;
        uint8_t hopCount;
        uint8_t childCount;
        uint8_t statusFlags;
        uint8_t reserved;
    };

    while(true)
    {
        CurrentTimeUs = esp_timer_get_time();

        bool IsConnectedToParentLocal = false;
        bool ApIpAcquiredLocal = false;
        bool IsMasterFoundLocal = false;
        uint8_t MyHopCountLocal = 255;
        uint8_t ChildCountLocal = 0;
        char ParentIpLocal[16]{};
        char MyApIpLocal[16]{};
        if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
        IsConnectedToParentLocal = ApStaClassInstance->IsConnectedToParent;
        ApIpAcquiredLocal = ApStaClassInstance->ApIpAcquired;
        IsMasterFoundLocal = ApStaClassInstance->IsMasterFound;
        MyHopCountLocal = ApStaClassInstance->MyHopCount;
        ChildCountLocal = static_cast<uint8_t>(ApStaClassInstance->ChildDevices.size());
        strncpy(ParentIpLocal, ApStaClassInstance->ParentDevice.IpAddress, sizeof(ParentIpLocal) - 1);
        ParentIpLocal[sizeof(ParentIpLocal) - 1] = '\0';
        strncpy(MyApIpLocal, ApStaClassInstance->MyApIpAddress, sizeof(MyApIpLocal) - 1);
        MyApIpLocal[sizeof(MyApIpLocal) - 1] = '\0';
        if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);

        if (IsConnectedToParentLocal &&
            ApIpAcquiredLocal &&
            (CurrentTimeUs - LastHeartbeatTimeUs >= HEARTBEAT_PULSE_US))
        {
            const uint8_t HeartbeatValue = 79;
            uint8_t TxBuffer[64]{};

            const size_t PacketLength = ApStaClassInstance->CreatePacket(&HeartbeatValue, sizeof(HeartbeatValue), 79, TxBuffer, sizeof(TxBuffer));
            if (PacketLength > 0)
            {
                sockaddr_in Destination{};
                Destination.sin_family = AF_INET;
                Destination.sin_port   = htons(ApStaClassInstance->UdpPort);

                const char* HeartbeatTargetIp =
                    IsMasterFoundLocal ? "192.168.0.254" : ParentIpLocal;

                if (HeartbeatTargetIp != nullptr &&
                    HeartbeatTargetIp[0] != '\0' &&
                    inet_pton(AF_INET, HeartbeatTargetIp, &Destination.sin_addr) == 1)
                {
                    HeartbeatAttemptCount++;
                    if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
                    // Only latch send time when starting a new request/reply cycle.
                    // If no reply has arrived yet, keep the original send timestamp
                    // so timeout can expire instead of sliding forever.
                    const int64_t LastRxUs = ApStaClassInstance->LastHeartbeatUs;
                    const int64_t LastTxUs = ApStaClassInstance->LastHeartbeatSentUs;
                    if (LastTxUs == 0 || LastRxUs >= LastTxUs)
                    {
                        ApStaClassInstance->LastHeartbeatSentUs = static_cast<int64_t>(CurrentTimeUs);
                    }
                    if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);

                    if (strcmp(HeartbeatTargetIp, MyApIpLocal) == 0)
                    {
                        if (ApStaClassInstance->IsRuntimeLoggingEnabled)
                        {
                            ESP_LOGE(STA_TAG, "Heartbeat target equals local AP IP (%s). Packet not sent.", HeartbeatTargetIp);
                        }
                    }
                    else
                    {
                        const size_t Sent = ApStaClassInstance->SendData(TxBuffer, static_cast<int>(PacketLength), Destination);
                        LastHeartbeatTimeUs = CurrentTimeUs;
                        if (Sent > 0)
                        {
                            HeartbeatSentCount++;
                        }
                        else
                        {
                            HeartbeatSendFailCount++;
                            if (ApStaClassInstance->IsRuntimeLoggingEnabled)
                            {
                                ESP_LOGW(STA_TAG, "Heartbeat send failed to %s", HeartbeatTargetIp);
                            }
                        }

                        if (ApStaClassInstance->IsRuntimeLoggingEnabled)
                        {
                            ESP_LOGI(STA_TAG, "Heartbeat sent to %s", HeartbeatTargetIp);
                        }
                    }
                }
            }
        }

        if (IsConnectedToParentLocal &&
            ApIpAcquiredLocal &&
            (CurrentTimeUs - LastSystemInfoTimeUs >= SYSTEM_INFO_PULSE_US))
        {
            SystemInfoPayload Payload{};
            Payload.uptimeUs = CurrentTimeUs;
            Payload.hopCount = MyHopCountLocal;
            Payload.childCount = ChildCountLocal;
            Payload.statusFlags = 0;
            if (IsConnectedToParentLocal) Payload.statusFlags |= 0x01;
            if (ApIpAcquiredLocal) Payload.statusFlags |= 0x02;
            if (IsMasterFoundLocal) Payload.statusFlags |= 0x04;

            uint8_t TxBuffer[96]{};
            const size_t PacketLength = ApStaClassInstance->CreatePacket(
                reinterpret_cast<const uint8_t*>(&Payload),
                sizeof(Payload),
                80,
                TxBuffer,
                sizeof(TxBuffer));

            if (PacketLength > 0)
            {
                // Mark as generic upstream transit traffic so every node forwards.
                TxBuffer[43] = 2;

                sockaddr_in Destination{};
                Destination.sin_family = AF_INET;
                Destination.sin_port   = htons(ApStaClassInstance->UdpPort);

                const char* SystemInfoTargetIp =
                    IsMasterFoundLocal ? "192.168.0.254" : ParentIpLocal;

                if (SystemInfoTargetIp != nullptr &&
                    SystemInfoTargetIp[0] != '\0' &&
                    strcmp(SystemInfoTargetIp, MyApIpLocal) != 0 &&
                    inet_pton(AF_INET, SystemInfoTargetIp, &Destination.sin_addr) == 1)
                {
                    const size_t Sent = ApStaClassInstance->SendData(TxBuffer, static_cast<int>(PacketLength), Destination);
                    LastSystemInfoTimeUs = CurrentTimeUs;
                    if (Sent > 0)
                    {
                        SystemInfoSentCount++;
                    }
                    else if (ApStaClassInstance->IsRuntimeLoggingEnabled)
                    {
                        ESP_LOGW(STA_TAG, "SystemInfo send failed to %s", SystemInfoTargetIp);
                    }

                    if (ApStaClassInstance->IsRuntimeLoggingEnabled)
                    {
                        ESP_LOGI(STA_TAG, "SystemInfo sent upstream to %s | hop=%u child=%u flags=0x%02X",
                                 SystemInfoTargetIp,
                                 Payload.hopCount,
                                 Payload.childCount,
                                 Payload.statusFlags);
                    }
                }
            }
        }

        if (ApStaClassInstance->IsRuntimeLoggingEnabled &&
            (CurrentTimeUs - LastTxDiagUs >= 5000000))
        {
            ESP_LOGW(STA_TAG, "TX diag | hb_attempt=%lu hb_sent=%lu hb_fail=%lu sys_sent=%lu",
                     static_cast<unsigned long>(HeartbeatAttemptCount),
                     static_cast<unsigned long>(HeartbeatSentCount),
                     static_cast<unsigned long>(HeartbeatSendFailCount),
                     static_cast<unsigned long>(SystemInfoSentCount));
            LastTxDiagUs = CurrentTimeUs;
        }

        vTaskDelay(1);
    }

    vTaskDelete(nullptr);
}



bool AccessPointStation::StartUdp(uint16_t Port, uint8_t Core)
{
    if (ApStaClassInstance->UdpStarted) return true;
    
    // We only start UDP if we have an IP (either as an AP or a STA)
    bool IsConnectedLocal = false;
    bool HasAnyChildrenLocal = false;
    bool HasIpLocal = false;
    if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreTake(ApStaClassInstance->StateMutex, portMAX_DELAY);
    IsConnectedLocal = ApStaClassInstance->IsConnectedToParent;
    HasAnyChildrenLocal = !ApStaClassInstance->ChildDevices.empty();
    HasIpLocal = ApStaClassInstance->ApIpAcquired;
    if (ApStaClassInstance->StateMutex != nullptr) xSemaphoreGive(ApStaClassInstance->StateMutex);

    if (!IsConnectedLocal && !HasAnyChildrenLocal) 
    {
        if (!HasIpLocal)
        {
            if (ApStaClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGW("UDP", "StartUdp skipped: no link, no children, no IP");
            }
            return false;
        }
    }
    
    if (Port == 0)
    {
        if (ApStaClassInstance->IsRuntimeLoggingEnabled)
        {
            ESP_LOGE("UDP", "StartUdp failed: invalid port 0");
        }
        return false;
    }

    // Create socket
    ApStaClassInstance->UdpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (ApStaClassInstance->UdpSocket < 0)
    {
        if (ApStaClassInstance->IsRuntimeLoggingEnabled)
        {
            ESP_LOGE("UDP", "StartUdp failed: socket() errno=%d", errno);
        }
        ApStaClassInstance->UdpSocket = -1;
        return false;
    }

    int ReuseAddr = 1;
    setsockopt(ApStaClassInstance->UdpSocket, SOL_SOCKET, SO_REUSEADDR, &ReuseAddr, sizeof(ReuseAddr));

    // Bind
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(Port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // Listens on both AP and STA interfaces

    if (bind(ApStaClassInstance->UdpSocket, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        if (ApStaClassInstance->IsRuntimeLoggingEnabled)
        {
            ESP_LOGE("UDP", "StartUdp failed: bind() port=%u errno=%d", Port, errno);
        }
        close(ApStaClassInstance->UdpSocket);
        ApStaClassInstance->UdpSocket = -1;
        return false;
    }

    // Socket Timeout (Essential for the RX task loop to breathe)
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000; // Increased to 10ms for better CPU efficiency in dual-mode
    if (setsockopt(ApStaClassInstance->UdpSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0 &&
        ApStaClassInstance->IsRuntimeLoggingEnabled)
    {
        ESP_LOGW("UDP", "StartUdp warning: setsockopt(SO_RCVTIMEO) errno=%d", errno);
    }

    if (ApStaClassInstance->ReceiveTaskHandle != nullptr) 
    {
        vTaskDelete(ApStaClassInstance->ReceiveTaskHandle);
        ApStaClassInstance->ReceiveTaskHandle = nullptr;
    }

    if (ApStaClassInstance->TransmitTaskHandle != nullptr) 
    {
        vTaskDelete(ApStaClassInstance->TransmitTaskHandle);
        ApStaClassInstance->TransmitTaskHandle = nullptr;
    }

    if (xTaskCreatePinnedToCore(&AccessPointStation::ReceiveTask,
                                "ApStaUdpRx",
                                8192,
                                nullptr,
                                5,
                                &ApStaClassInstance->ReceiveTaskHandle,
                                Core) != pdPASS)
    {
        if (ApStaClassInstance->IsRuntimeLoggingEnabled)
        {
            ESP_LOGE("UDP", "StartUdp failed: create RX task");
        }
        close(ApStaClassInstance->UdpSocket);
        ApStaClassInstance->UdpSocket = -1;
        ApStaClassInstance->ReceiveTaskHandle = nullptr;
        return false;
    }

    if (xTaskCreatePinnedToCore(&AccessPointStation::TransmitTask,
                                "ApStaUdpTx",
                                8192,
                                nullptr,
                                5,
                                &ApStaClassInstance->TransmitTaskHandle,
                                Core) != pdPASS)
    {
        if (ApStaClassInstance->IsRuntimeLoggingEnabled)
        {
            ESP_LOGE("UDP", "StartUdp failed: create TX task");
        }
        if (ApStaClassInstance->ReceiveTaskHandle != nullptr)
        {
            vTaskDelete(ApStaClassInstance->ReceiveTaskHandle);
            ApStaClassInstance->ReceiveTaskHandle = nullptr;
        }

        close(ApStaClassInstance->UdpSocket);
        ApStaClassInstance->UdpSocket = -1;
        ApStaClassInstance->TransmitTaskHandle = nullptr;
        return false;
    }

    ApStaClassInstance->UdpStarted = true;
    if (ApStaClassInstance->IsRuntimeLoggingEnabled) ESP_LOGI("UDP", "Transmit and Receive tasks started on Port %d", Port);
    
    return true;
}

bool AccessPointStation::StopUdp()
{
    if (!ApStaClassInstance->UdpStarted) return true;

    ApStaClassInstance->UdpStarted = false;

    if (ApStaClassInstance->ReceiveTaskHandle != nullptr)
    {
        vTaskDelete(ApStaClassInstance->ReceiveTaskHandle);
        ApStaClassInstance->ReceiveTaskHandle = nullptr;
    }

    if (ApStaClassInstance->TransmitTaskHandle != nullptr)
    {
        vTaskDelete(ApStaClassInstance->TransmitTaskHandle);
        ApStaClassInstance->TransmitTaskHandle = nullptr;
    }

    if (ApStaClassInstance->UdpSocket >= 0)
    {
        // shutdown() ensures all pending sends/receives are terminated
        shutdown(ApStaClassInstance->UdpSocket, SHUT_RDWR);
        close(ApStaClassInstance->UdpSocket);
        ApStaClassInstance->UdpSocket = -1;
    }

    if (ApStaClassInstance->IsRuntimeLoggingEnabled)
    {
        ESP_LOGW("UDP", "Transmit and Receive tasks Stopped");
    }

    return true;
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
                    "node%llu", (unsigned long long)MY_UID);
                
            ApWifiServiceConfig.ap.ssid_len = strlen((char*)ApWifiServiceConfig.ap.ssid);

            // Ensure password is set and is at least 8 characters
            strncpy((char*)ApWifiServiceConfig.ap.password, MY_PASS, 63);

            ApWifiServiceConfig.ap.max_connection = MAX_STA_CONN; 
            
            ApWifiServiceConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
            ApWifiServiceConfig.ap.channel = 6; 

            // Configure AP netif with a UID-derived subnet so every node AP
            // uses a distinct gateway address. This avoids AP/STA IP collisions
            // (for example many nodes defaulting to 192.168.4.1).
            if (ApStaClassInstance->ApNetif != nullptr)
            {
                const uint8_t SubnetOctet = static_cast<uint8_t>(20 + (MY_UID % 200));
                esp_netif_ip_info_t ApIpInfo{};
                IP4_ADDR(&ApIpInfo.ip, 192, 168, SubnetOctet, 1);
                IP4_ADDR(&ApIpInfo.gw, 192, 168, SubnetOctet, 1);
                IP4_ADDR(&ApIpInfo.netmask, 255, 255, 255, 0);

                // Best effort stop before setting static IP.
                esp_netif_dhcps_stop(ApStaClassInstance->ApNetif);
                if (esp_netif_set_ip_info(ApStaClassInstance->ApNetif, &ApIpInfo) != ESP_OK) return false;
                if (esp_netif_dhcps_start(ApStaClassInstance->ApNetif) != ESP_OK) return false;

                snprintf(ApStaClassInstance->MyApIpAddress,
                         sizeof(ApStaClassInstance->MyApIpAddress),
                         "192.168.%u.1",
                         static_cast<unsigned>(SubnetOctet));

                if (ApStaClassInstance->IsRuntimeLoggingEnabled)
                {
                    ESP_LOGW(STA_TAG, "AP subnet configured: %s/24", ApStaClassInstance->MyApIpAddress);
                }
            }
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

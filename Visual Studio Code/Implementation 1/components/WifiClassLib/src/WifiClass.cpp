#include "WifiClass.h"
#include "esp_wifi_types_generic.h"
#include "freertos/idf_additions.h"
#include "lwip/sockets.h"
#include "portmacro.h"
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
        child.LastHeartbeatUs = child.TimeOfConnection;
        child.ChildrenCount = 0;
        memcpy(child.MacId, Event->mac, 6);
        child.IpAddress[0] = '\0'; 

        ApStaClassInstance->ChildDevices.push_back(child);

        if (ApStaClassInstance->IsRuntimeLoggingEnabled) 
        {
            ESP_LOGW("MESH_AP", "Child Joined | MAC: " MACSTR " | AID: %d", 
                     MAC2STR(Event->mac), Event->aid);
        }

        ApStaClassInstance->UpdateBeaconMetadata(
            ApStaClassInstance->MyHopCount,
            (uint8_t)ApStaClassInstance->ChildDevices.size()
        );
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

        ApStaClassInstance->UpdateBeaconMetadata(
            ApStaClassInstance->MyHopCount,
            (uint8_t)ApStaClassInstance->ChildDevices.size()
        );
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

            if (ApStaClassInstance->IsConnectedToParent &&
                ApStaClassInstance->ApIpAcquired &&
                ApStaClassInstance->IsCandidateValid)
            {

                const uint8_t curHop = ApStaClassInstance->ParentDevice.HopCount;
                const uint8_t newHop = ApStaClassInstance->CandidateHop;

                if (newHop < curHop) 
                {
                    ApStaClassInstance->RoamRequested = true;
                    if (ApStaClassInstance->IsRuntimeLoggingEnabled) 
                    {
                        ESP_LOGW(STA_TAG, "Roam requested: current hop %u -> candidate hop %u",
                                curHop, newHop);
                    }
                }
            }

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
            ApStaClassInstance->ParentWifiRecord.ssid[0] = '\0';
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
            ApStaClassInstance->UpdateBeaconMetadata(ApStaClassInstance->MyHopCount, (uint8_t)ApStaClassInstance->ChildDevices.size());

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

    if (data->length < 4) return;
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
        // If macid is already in system
        if (memcmp(ApStaClassInstance->CallbackIeData[i].MacId, sa, 6)) return;

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
    my_ie.header.length = 5; // 3 (OUI) + 2 (Payload)
    my_ie.header.vendor_oui[0] = MESH_OUI_0;
    my_ie.header.vendor_oui[1] = MESH_OUI_1;
    my_ie.header.vendor_oui[2] = MESH_OUI_2;
    my_ie.payload[0] = Hop;
    my_ie.payload[1] = Children;

    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, nullptr);
    esp_wifi_set_vendor_ie(false, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_1, nullptr);

    esp_err_t res_bcn = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_BEACON, WIFI_VND_IE_ID_0, (vendor_ie_data_t*)&my_ie);
    esp_err_t res_prb = esp_wifi_set_vendor_ie(true, WIFI_VND_IE_TYPE_PROBE_RESP, WIFI_VND_IE_ID_1, (vendor_ie_data_t*)&my_ie);

    if (IsRuntimeLoggingEnabled) 
    {
        if (res_bcn == ESP_OK && res_prb == ESP_OK) 
        {
            ESP_LOGW(STA_TAG, "Mesh IE Broadcast Updated: Hop %d, Children %d", Hop, Children);
        } 
        else 
        {
            ESP_LOGE(STA_TAG, "IE Update Deferred (Driver Busy): 0x%x", res_bcn);
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
            bool foundVendorData = false;
            
            for (int j = 0; j < 20; j++) 
            {
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
        IsCandidateValid = true;
        IsCandidateMaster = MasterFound;
        CandidateWifiRecord = *BestAp;
        CandidateHop = CurrentBestHop;
        CandidateChildren = CurrentBestChildren;

        if (!IsConnectedToParent && !IsConnecting)
        {
            IsMasterFound = IsCandidateMaster;
            ParentWifiRecord = CandidateWifiRecord;
            ParentDevice.HopCount = CandidateHop;
            ParentDevice.ChildrenCount = CandidateChildren;

            MyHopCount = (CandidateHop == 255) ? 255 : (uint8_t)(CandidateHop + 1);
        }
    }

    else
    {
        IsCandidateValid = false;
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
        ESP_LOGW(STA_TAG, "ConnectToBestAp: Master NOT found. Connecting to Mesh Parent SSID: %s | Pass: %s", (char*)ParentWifiRecord.ssid, MY_PASS);
        memcpy(sta_config.sta.ssid, ParentWifiRecord.ssid, sizeof(sta_config.sta.ssid));
        strncpy((char*)sta_config.sta.password, MY_PASS, sizeof(sta_config.sta.password));
    }


    sta_config.sta.bssid_set = true;
    memcpy(sta_config.sta.bssid, ParentWifiRecord.bssid, 6);
    sta_config.sta.channel = ParentWifiRecord.primary;


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
            if (!ApStaClassInstance->IsScanning && !ApStaClassInstance->IsConnecting) ApStaClassInstance->InitiateMeshScan();

            
            if (!ApStaClassInstance->IsConnectedToParent &&
                !ApStaClassInstance->IsConnecting &&
                ApStaClassInstance->ParentWifiRecord.ssid[0] != '\0')
            {
                ApStaClassInstance->IsConnecting = true;
                ApStaClassInstance->ConnectToBestAp();
            }

            if (ApStaClassInstance->IsConnectedToParent &&
                !ApStaClassInstance->IsConnecting &&
                ApStaClassInstance->RoamRequested &&
                ApStaClassInstance->IsCandidateValid)
            {
                ApStaClassInstance->RoamRequested = false;
                ApStaClassInstance->IsConnecting = true;

                ApStaClassInstance->IsMasterFound = ApStaClassInstance->IsCandidateMaster;
                ApStaClassInstance->ParentWifiRecord = ApStaClassInstance->CandidateWifiRecord;
                ApStaClassInstance->ParentDevice.HopCount = ApStaClassInstance->CandidateHop;
                ApStaClassInstance->MyHopCount =
                    (ApStaClassInstance->CandidateHop == 255) ? 255 : (uint8_t)(ApStaClassInstance->CandidateHop + 1);

                ESP_LOGW(STA_TAG, "Roaming now to %s (hop %u)",
                        (char*)ApStaClassInstance->ParentWifiRecord.ssid,
                        ApStaClassInstance->ParentDevice.HopCount);

                esp_wifi_disconnect();
                vTaskDelay(pdMS_TO_TICKS(200));
                ApStaClassInstance->ConnectToBestAp();
            }
        }


        // 0.5s
        if (Counter % 5 == 0)
        {
            if (ApStaClassInstance->IsConnectedToParent && ApStaClassInstance->ApIpAcquired)
            {
                uint8_t TxBuffer[64]{};
                uint8_t HeartbeatValue = 79;
                size_t Length = ApStaClassInstance->CreatePacket(&HeartbeatValue, 1, 255, TxBuffer, sizeof(TxBuffer));
            
                if (Length > 48)
                {
                    sockaddr_in Destination{};
                    Destination.sin_family = AF_INET;
                    Destination.sin_port   = htons(ApStaClassInstance->UdpPort);
                    if (ApStaClassInstance->IsMasterFound)
                    {
                        inet_pton(AF_INET, "192.168.0.254",
                                &Destination.sin_addr);
                    }
                    else 
                    {
                        inet_pton(AF_INET, ApStaClassInstance->ParentDevice.IpAddress,
                                &Destination.sin_addr);
                    }
                    int Sent = sendto(ApStaClassInstance->UdpSocket,
                            TxBuffer,
                            Length,
                            0,
                            (sockaddr*)&Destination,
                            sizeof(Destination));

                    if (Sent < 0 && ApStaClassInstance->IsRuntimeLoggingEnabled) ESP_LOGE(STA_TAG, "Heartbeat sendto failed (errno=%d)", errno);
                }
            }
        }
    }

    vTaskDelete(NULL);
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
    TempHeader.payloadSize = DataLength;
    TempHeader.slaveUid = CONFIG_ESP_NODE_UID;
    //TempHeader.messageCounter = 0;
    TempHeader.senderTimestampUs = (uint64_t)esp_timer_get_time();
    TempHeader.prevCycleTimeUs = 0;
    TempHeader.chainedSlaveCount = 0;
    TempHeader.PacketType = PacketType;
    TempHeader.flags = 0;
    TempHeader.headerVersion = 1;
    TempHeader.networkId = 0;
    TempHeader.chainDistance = 0;
    TempHeader.ttl = 10;
    TempHeader.crc32 = 0;

    uint8_t* p = PacketOut;

    memcpy(p, &TempHeader, sizeof(PacketHeader));
    memcpy(p + sizeof(PacketHeader), DataToInclude, DataLength);
    memcpy(p + sizeof(PacketHeader) + DataLength, &PACKET_END_DELIMITER, 2);

    return PACKET_HEADER_SIZE + DataLength + sizeof(PACKET_END_DELIMITER);
}


void AccessPointStation::ProcessData(uint8_t* data, int length)
{
    if (data == nullptr || length <= 48) return;
    if (data[0] != 0xB5 || data[1] != 0x02) return;

    constexpr int headerSize = 48;
    constexpr int terminatorSize = 2;
    uint16_t PayloadSize = (static_cast<uint16_t>(data[2]) << 8) | static_cast<uint16_t>(data[3]);
    int terminatorIndex = headerSize + PayloadSize;

    if (length < terminatorIndex + terminatorSize) return;
    if (data[terminatorIndex] != 0x03 || data[terminatorIndex + 1] != 0x5B) return;

    uint8_t PacketType = data[37];


    
    switch(PacketType)
    {
        case 0xFF:  // Heartbeat
            LastHeartbeatUs = esp_timer_get_time();
            break;

        default:
            break;
    }

}

size_t AccessPointStation::PrepareTxPacket(const uint8_t* rxData,
                                         int rxLength,
                                         uint8_t* txBuffer,
                                         int& txLength)
{

    txLength = 0;

    if (!rxData || !txBuffer) return 0;
    if (rxLength < 48) return 0;

    constexpr int headerSize = 48;
    constexpr int terminatorSize = 2;
    const uint8_t PacketType = rxData[37];
    const uint8_t ForwardMode  = rxData[43];

    uint16_t PayloadSize = (static_cast<uint16_t>(rxData[2]) << 8) | static_cast<uint16_t>(rxData[3]);
    int ExpectedSize = headerSize + PayloadSize + terminatorSize;
    int TermIndex = headerSize + PayloadSize;
    
    if (rxLength < ExpectedSize) return 0;
    if (rxData[TermIndex] != 0x03 || rxData[TermIndex + 1] != 0x5B) return 0;



    // FORWARD PACKET
    if (ForwardMode != 00) 
    {
        memcpy(txBuffer, rxData, ExpectedSize);
        txLength = ExpectedSize;
        return txLength;
    }


    
    // PROCESS PACKET
    else
    {
        if (PayloadSize > MaxPayload) return 0;

        const uint8_t* PayloadPtr = rxData + headerSize;

        // ====================================
        //      FILL INTERNAL DATA
        // ====================================

        PayloadSeq++;
        memcpy(LatestPayload, PayloadPtr, PayloadSize);
        LatestPayloadSize = PayloadSize;
        LatestPayloadType = PacketType;
        LatestPayloadUs   = esp_timer_get_time();
        PayloadSeq++; 

        return 0;
    }
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
            int MatchIndex = -1;

            for (size_t i = 0; i < ApStaClassInstance->ChildDevices.size(); i++)
            {
                // Compare 8-byte UID
                if (memcmp(&ApStaClassInstance->ChildDevices[i].UID, DestinationUid, 8) == 0)
                {
                    MatchIndex = i;
                    break;
                }
            }

            if (MatchIndex < 0) return false;

            sockaddr_in Destination{};
            Destination.sin_family = AF_INET;
            Destination.sin_port   = htons(ApStaClassInstance->UdpPort);

            if (inet_pton(AF_INET,
              ApStaClassInstance->ChildDevices[MatchIndex].IpAddress,
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

            if (IsMasterFound)
            {
                if (inet_pton(AF_INET, "192.168.0.254", &Destination.sin_addr) != 1)
                {
                    return false;
                }
            }
            else 
            {
                if (inet_pton(AF_INET,
                  ApStaClassInstance->ParentDevice.IpAddress,
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

void AccessPointStation::ReceiveTask(void* pvParameters)
{
    uint8_t ReceiveBuffer[1500];
    int ReceivedBytes = 0;
    uint8_t SendBuffer[1500];
    int SendBytes = 0;

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
            ApStaClassInstance->ProcessData(ReceiveBuffer, ReceivedBytes);

            SendBytes = 0;

            ApStaClassInstance->PrepareTxPacket(ReceiveBuffer, ReceivedBytes, SendBuffer, SendBytes);

            if (SendBytes <= 0)
            {
                continue;
            }

            if (!ApStaClassInstance->DetermineDestinationAddress(SourceAddress, SendBuffer, SendBytes, DestinationAddress))
            {
                continue;
            }

            ApStaClassInstance->SendData(SendBuffer, SendBytes, DestinationAddress);
        }

        vTaskDelay(1);
    }

    vTaskDelete(nullptr);
}

void AccessPointStation::TransmitTask(void* pvParameters)
{
    uint8_t localBuf[1500];
    int localLen = 0;

    TickType_t LastHeartbeat{};

    while(true)
    {
        bool Send = false;

        portENTER_CRITICAL(&ApStaClassInstance->TxCriticalSection);

        if (ApStaClassInstance->PreparedTxLength > 0)
        {
            localLen = ApStaClassInstance->PreparedTxLength;
            memcpy(localBuf, ApStaClassInstance->PreparedTxPacket, localLen);
            ApStaClassInstance->PreparedTxLength = 0;
            memset(ApStaClassInstance->PreparedTxPacket, 0, sizeof(ApStaClassInstance->PreparedTxPacket));

            Send = true;
        }

        portEXIT_CRITICAL(&ApStaClassInstance->TxCriticalSection);

        if (Send)
        {
            Send = false;
            sockaddr_in Destination{};
            Destination.sin_family = AF_INET;
            Destination.sin_port   = htons(ApStaClassInstance->UdpPort);

            if (ApStaClassInstance->IsMasterFound) inet_pton(AF_INET, "192.168.0.254", &Destination.sin_addr);
            else inet_pton(AF_INET, ApStaClassInstance->ParentDevice.IpAddress, &Destination.sin_addr);

            sendto(ApStaClassInstance->UdpSocket,
                   localBuf,
                   localLen,
                   0,
                   (const sockaddr*)&Destination,
                   sizeof(Destination));
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    vTaskDelete(nullptr);
}



bool AccessPointStation::StartUdp(uint16_t Port, uint8_t Core)
{
    if (ApStaClassInstance->UdpStarted) return true;
    
    // We only start UDP if we have an IP (either as an AP or a STA)
    if (!ApStaClassInstance->IsConnectedToParent && ApStaClassInstance->ChildDevices.empty()) 
    {
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
                                4096,
                                nullptr,
                                5,
                                &ApStaClassInstance->ReceiveTaskHandle,
                                Core) != pdPASS)
    {
        close(ApStaClassInstance->UdpSocket);
        ApStaClassInstance->UdpSocket = -1;
        ApStaClassInstance->ReceiveTaskHandle = nullptr;
        return false;
    }

    if (xTaskCreatePinnedToCore(&AccessPointStation::TransmitTask,
                                "ApStaUdpTx",
                                4096,
                                nullptr,
                                5,
                                &ApStaClassInstance->TransmitTaskHandle,
                                Core) != pdPASS)
    {
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
        vTaskDelete(ApStaClassInstance->TransmitTaskHandle);
        ApStaClassInstance->ReceiveTaskHandle = nullptr;
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
                    "node%d", CONFIG_ESP_NODE_UID);
                
            ApWifiServiceConfig.ap.ssid_len = strlen((char*)ApWifiServiceConfig.ap.ssid);

            // Ensure password is set and is at least 8 characters
            strncpy((char*)ApWifiServiceConfig.ap.password, MY_PASS, 63);

            ApWifiServiceConfig.ap.max_connection = MAX_STA_CONN; 
            
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
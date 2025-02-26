#include "WifiClass.h"

// Author - Ben Sturdy
// This file implements a class 'Wifi Class'. This class should be instantiated
// only once in a project. This class controls all wireless functionalities.
// This class can set up a system as an Access Point or a Station in WiFi mode. 
// This class can set up and utilise ESP-NOW. The functions with this class can 
// run on the same core as other processes.





//==============================================================================// 
//                                                                              //
//                            Wifi Class                                        //
//                                                                              //
//==============================================================================// 

#define WIFI_SSID                   CONFIG_ESP_WIFI_SSID
#define WIFI_PASS                   CONFIG_ESP_WIFI_PASSWORD
#define WIFI_CHANNEL                CONFIG_ESP_WIFI_CHANNEL
#define MAX_STA_CONN                CONFIG_ESP_MAX_STA_CONN
#define WIFI_MODE                   CONFIG_ESP_DEVICE_MODE
#define UDP_PORT                    CONFIG_ESP_UDP_PORT

#define EspNowTaskPriority          24
#define UdpPollingTaskPriority      1
#define UdpProcessingTaskPriority   23
#define UdpSystemTaskPriority       22

#define TAG                         "Wifi Class"

static WifiClass* ClassInstance;

StackType_t WifiClass::EspNowTaskStack[EspNowTaskStackSize];
StaticTask_t WifiClass::EspNowTaskTCB;

StackType_t WifiClass::UdpPollingTaskStack[UdpPollingTaskStackSize];
StaticTask_t WifiClass::UdpPollingTaskTCB;

StackType_t WifiClass::UdpProcessingTaskStack[UdpProcessingTaskStackSize];
StaticTask_t WifiClass::UdpProcessingTaskTCB;

StackType_t WifiClass::UdpSystemTaskStack[UdpSystemTaskStackSize];
StaticTask_t WifiClass::UdpSystemTaskTCB;





//==============================================================================//
//                                                                              //
//            Constructors, Destructors, Internal Functions                     //
//                                                                              //
//==============================================================================// 

// Constructor
WifiClass::WifiClass()
{
    ClassInstance = this;
    EspNowDeviceQueue = NULL;    
}

// Destructor (Unsused, this class should exist throughout runtime)
WifiClass::~WifiClass()
{
    ;
}





// Software event handler for wifi AP events
void WifiClass::WifiEventHandlerAp(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) 
    {
        if (ClassInstance->IsRuntimeLoggingEnabled)
        {
            printf("\n");
            ESP_LOGW(TAG, "WI-FI CONNCETION EVENT");
        }

        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
   
        WifiDevice NewDevice;
        NewDevice.TimeOfConnection = esp_timer_get_time();
        memcpy(NewDevice.MacId, event->mac, sizeof(NewDevice.MacId));
        NewDevice.IsRegisteredWithEspNow = false;
        NewDevice.aid = event->aid;

        ClassInstance->ClientWifiDeviceList.push_back(NewDevice);

        BaseType_t higherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(ClassInstance->EspNowDeviceQueue, &NewDevice, &higherPriorityTaskWoken);
    } 
    
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) 
    {
        if (ClassInstance->IsRuntimeLoggingEnabled)
        {
            printf("\n");
            ESP_LOGW(TAG, "WI-FI DISCONNCETION EVENT");
        }

        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);

        auto it = std::find_if(ClassInstance->ClientWifiDeviceList.begin(), ClassInstance->ClientWifiDeviceList.end(),
                                [event](const WifiDevice& device)
                                {
                                    return memcmp(device.MacId, event->mac, sizeof(device.MacId)) == 0;
                                });
        
        if (it != ClassInstance->ClientWifiDeviceList.end()) 
        {
            // Device found, remove it
            WifiDevice TempDevice = *it;

            TempDevice.IsRegisteredWithEspNow = true;

            ClassInstance->ClientWifiDeviceList.erase(it);  

            BaseType_t higherPriorityTaskWoken = pdFALSE;

            xQueueSendFromISR(ClassInstance->EspNowDeviceQueue, &TempDevice, &higherPriorityTaskWoken);
        }
    }
}

// Software event handler for wifi STA events
void WifiClass::WifiEventHandlerSta(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_STA_CONNECTED) 
    {
        if (ClassInstance->IsRuntimeLoggingEnabled)
        {
            printf("\n");
            ESP_LOGW(TAG, "WI-FI CONNCETION EVENT");
        }

        wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*)event_data;

        WifiDevice NewDevice;
        NewDevice.TimeOfConnection = esp_timer_get_time();
        NewDevice.IsRegisteredWithEspNow = false;
        NewDevice.aid = event->aid;
        
        // Get the MAC address of the ESP32 station
        uint8_t mac[6];
        esp_err_t result = esp_wifi_get_mac(WIFI_IF_STA, mac);
        if (result == ESP_OK)
        {
            memcpy(NewDevice.MacId, mac, sizeof(NewDevice.MacId));
        }
        else if (ClassInstance->IsRuntimeLoggingEnabled)
        {
            ESP_LOGE(TAG, "Failed to get MAC address for STA mode");
        }

        ClassInstance->HostWifiDevice = NewDevice;

        ClassInstance->IsConnectedToAP = true;

        BaseType_t higherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(ClassInstance->EspNowDeviceQueue, &NewDevice, &higherPriorityTaskWoken);

        ESP_LOGI(TAG, "Connected to Wi-Fi!");
    }

    else if (event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        if (ClassInstance->IsRuntimeLoggingEnabled)
        {
            printf("\n");
            ESP_LOGW(TAG, "WI-FI DISCONNCETION EVENT");
        }

        if (ClassInstance->IsConnectedToAP)
        {
            WifiDevice TempDevice = ClassInstance->HostWifiDevice;

            TempDevice.IsRegisteredWithEspNow = true;

            memset(&ClassInstance->HostWifiDevice, 0, sizeof(ClassInstance->HostWifiDevice));

            BaseType_t higherPriorityTaskWoken = pdFALSE;

            xQueueSendFromISR(ClassInstance->EspNowDeviceQueue, &TempDevice, &higherPriorityTaskWoken);
        }

        ClassInstance->IsConnectedToAP = false;

        ESP_LOGI(TAG, "Disconnected from Wi-Fi, retrying...");
        esp_wifi_disconnect();
        esp_wifi_connect();  // Retry connection immediately
    } 
}

// Software event handler for IP AP events
void WifiClass::IpEventHandlerAp(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_AP_STAIPASSIGNED)
    {
        if (ClassInstance->IsRuntimeLoggingEnabled)
        {
            printf("\n");
            ESP_LOGW(TAG, "IP ASSIGNED EVENT");
        }

        ip_event_ap_staipassigned_t* event = (ip_event_ap_staipassigned_t*) event_data;
        
        // Convert the assigned IP to a string
        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip, ip_str, sizeof(ip_str));

        ESP_LOGI(TAG, "Station assigned IP: %s", ip_str);

        // Loop through the ClientWifiDeviceList and find the device by MAC address
        for (auto& device : ClassInstance->ClientWifiDeviceList)
        {
            // Match by MAC address
            if (memcmp(device.MacId, event->mac, sizeof(device.MacId)) == 0)
            {
                // Found the device, store the IP as a string in the corresponding slot
                strncpy(device.IpAddress, ip_str, sizeof(device.IpAddress) - 1);
                device.IpAddress[sizeof(device.IpAddress) - 1] = '\0'; // Ensure null-termination

                ESP_LOGI(TAG, "Device " MACSTR " now has IP: %s", MAC2STR(event->mac), device.IpAddress);
                break;
            }
        }
    }
}

// Software event handler for IP STA events
void WifiClass::IpEventHandlerSta(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) 
    {
        if (ClassInstance->IsRuntimeLoggingEnabled)
        {
            printf("\n");
            ESP_LOGW(TAG, "IP ASSIGNED EVENT");
        }

        char ip_str[16];

        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        esp_ip4addr_ntoa(&event->ip_info.gw, ip_str, sizeof(ip_str));

        ESP_LOGI(TAG, "Got IP from network (STA Mode): %s", ip_str);

        strncpy(ClassInstance->HostWifiDevice.IpAddress, ip_str, sizeof(ClassInstance->HostWifiDevice.IpAddress));
    }
}





// Function that deauthenticates a device
void WifiClass::DeauthenticateDeviceAp(const char* ipAddress)
{
    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "WI-FI MANUAL DISCONNCETION EVENT");
    }

    // Find the device in the list using the provided IP address
    auto it = std::find_if(ClassInstance->ClientWifiDeviceList.begin(), ClassInstance->ClientWifiDeviceList.end(),
                            [ipAddress](const WifiDevice& device)
                            {
                                return strcmp(device.IpAddress, ipAddress) == 0;
                            });

    if (it != ClassInstance->ClientWifiDeviceList.end()) 
    {
        // Device found, extract info
        WifiDevice TempDevice = *it;

        // Mark the device as deauthenticated (or perform other state changes if necessary)
        TempDevice.IsRegisteredWithEspNow = true;

        // Optionally, deauthenticate the device at the Wi-Fi level
        esp_err_t result = esp_wifi_deauth_sta(TempDevice.aid);
        if (result == ESP_OK)
        {
            ESP_LOGI(TAG, "Device with MAC "MACSTR" deauthenticated.", MAC2STR(TempDevice.MacId));
        }
        else
        {
            ESP_LOGE(TAG, "Failed to deauthenticate device with MAC "MACSTR". Error code: %d", MAC2STR(TempDevice.MacId), result);
        }

        // Remove the device from the device list
        ClassInstance->ClientWifiDeviceList.erase(it);  

        // Send the deauthenticated device info to the queue (if necessary)
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(ClassInstance->EspNowDeviceQueue, &TempDevice, &higherPriorityTaskWoken);
    }
    else
    {
        ESP_LOGW(TAG, "Device with IP: %s not found in device list.", ipAddress);
    }
}

// Function that deauthenticates a device
void WifiClass::DeauthenticateDeviceSta(const char* ipAddress)
{
    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "WI-FI MANUAL DISCONNCETION EVENT");
    }

    if (strcmp(ipAddress, ClassInstance->HostWifiDevice.IpAddress) == 0) 
    {

        // Mark the device as deauthenticated (or perform other state changes if necessary)
        ClassInstance->HostWifiDevice.IsRegisteredWithEspNow = true;

        // Optionally, deauthenticate the device at the Wi-Fi level
        esp_err_t result = esp_wifi_disconnect();
        if (result == ESP_OK)
        {
            ESP_LOGI(TAG, "Device with MAC "MACSTR" deauthenticated.", MAC2STR(ClassInstance->HostWifiDevice.MacId));
        }
        else
        {
            ESP_LOGE(TAG, "Failed to deauthenticate device with MAC "MACSTR". Error code: %d", MAC2STR(ClassInstance->HostWifiDevice.MacId), result);
        }

        // Send the deauthenticated device info to the queue (if necessary)
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(ClassInstance->EspNowDeviceQueue, &ClassInstance->HostWifiDevice, &higherPriorityTaskWoken);

        // Remove the device from the device list
        memset(&ClassInstance->HostWifiDevice, 0, sizeof(ClassInstance->HostWifiDevice));
    }

    else
    {
        ESP_LOGW(TAG, "Device with IP: %s not found in device list.", ipAddress);
    }
}

// Task that connects and disconnects devices from ESP-NOW registery
void WifiClass::EspNowTask(void *pvParameters)
{
    WifiDevice DeviceToAction;

    while(true)
    {
        if ((xQueueReceive(ClassInstance->EspNowDeviceQueue, &DeviceToAction, portMAX_DELAY) == pdPASS))
        {
            if (ClassInstance->IsRuntimeLoggingEnabled)
            {
                printf("\n");
                ESP_LOGW(TAG, "ESP-NOW TASK TRIGGERED");
            }

            if (!DeviceToAction.IsRegisteredWithEspNow)
            {
                if (ClassInstance->IsRuntimeLoggingEnabled)
                {
                    ESP_LOGI(TAG, "Processing new device registration in ESP-NOW task for MAC: " MACSTR, MAC2STR(DeviceToAction.MacId));
                }
                
                if (!ClassInstance->EspNowRegisterDevice(&DeviceToAction))
                {
                    ESP_LOGE(TAG, "Failed to register device with ESP-NOW");
                }
            }

            else if (DeviceToAction.IsRegisteredWithEspNow)
            {
                if (ClassInstance->IsRuntimeLoggingEnabled)
                {
                    ESP_LOGI(TAG, "Deleting device registration in ESP-NOW task for MAC: " MACSTR, MAC2STR(DeviceToAction.MacId));
                }

                if (!ClassInstance->EspNowDeleteDevice(&DeviceToAction))
                {
                    ESP_LOGE(TAG, "Failed to Delete device with ESP-NOW");
                }
            }

            if (ClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGW(TAG, "ESP-NOW TASK FINISHED");
                printf("\n");
            }
        }
    }
}

// Function that registers device with ESP-NOW
bool WifiClass::EspNowRegisterDevice(WifiDevice* DeviceToRegister)
{
    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "ESP-NOW REGISTER DEVICE");
    }

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));

    uint8_t peerMacAddr[] = 
    {
        DeviceToRegister->MacId[0], 
        DeviceToRegister->MacId[1],
        DeviceToRegister->MacId[2],
        DeviceToRegister->MacId[3],
        DeviceToRegister->MacId[4],
        DeviceToRegister->MacId[5]
    };

    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        ESP_LOGI(TAG, "Registering device with MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
            peerMacAddr[0], peerMacAddr[1], peerMacAddr[2],
            peerMacAddr[3], peerMacAddr[4], peerMacAddr[5]);
    }
    
    memcpy(peerInfo.peer_addr, peerMacAddr, 6);    

    peerInfo.channel = 0; 
    peerInfo.encrypt = false; 

    esp_err_t add_peer_status = esp_now_add_peer(&peerInfo);
    if (add_peer_status != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(add_peer_status));
        return false;
    }

    DeviceToRegister->IsRegisteredWithEspNow = true;

    return true;
}

// Function that deregisters device with ESP-NOW
bool WifiClass::EspNowDeleteDevice(WifiDevice* DeviceToDelete)
{
    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "ESP-NOW DELETE DEVICE");
    }

    // Prepare the MAC address for logging and deletion.
    uint8_t peerMacAddr[] = 
    {
        DeviceToDelete->MacId[0],
        DeviceToDelete->MacId[1],
        DeviceToDelete->MacId[2],
        DeviceToDelete->MacId[3],
        DeviceToDelete->MacId[4],
        DeviceToDelete->MacId[5]
    };

    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        ESP_LOGI(TAG, "Deleting device with MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
            peerMacAddr[0], peerMacAddr[1], peerMacAddr[2],
            peerMacAddr[3], peerMacAddr[4], peerMacAddr[5]);
    }

    // Call the ESP-NOW deletion function.
    esp_err_t del_peer_status = esp_now_del_peer(peerMacAddr);
    if (del_peer_status != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to delete peer: %s", esp_err_to_name(del_peer_status));
        return false;
    }

    DeviceToDelete->IsRegisteredWithEspNow = false;

    return true;
}





// Function that sets up a UDP socket
bool WifiClass::SetupUdpSocket(uint16_t UdpPort)
{
    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "UDP SOCKET BINDING");
    }

    UdpSocketFD = socket(AF_INET, SOCK_DGRAM, 0);
    if (UdpSocketFD < 0)
    {
        ESP_LOGE(TAG, "Failed to create UDP socket");
        return false; 
    }

    memset(&UdpHostServerAddress, 0, sizeof(UdpHostServerAddress));
    UdpHostServerAddress.sin_family = AF_INET;
    UdpHostServerAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    UdpHostServerAddress.sin_port = htons(UdpPort);

    if (bind(UdpSocketFD, (struct sockaddr *)&UdpHostServerAddress, sizeof(UdpHostServerAddress)) < 0) 
    {
        ESP_LOGE(TAG, "Failed to bind UDP socket");
        close(UdpSocketFD);
        return false;
    }

    return true;
}

// Task that polls for new UDP data received
void WifiClass::UdpPollingTask(void* pvParameters)
{

    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "UDP POLLING TASK BEGIN");
    }

    int PacketLength;
    struct sockaddr_in SourceAddress;
    socklen_t SourceAddressLength = sizeof(SourceAddress);
    UdpPacket ReceivedPacket = {};

    while(true)
    {
        if (ClassInstance->IsAp)
        {
            ClassInstance->UdpPollingOnAp(&SourceAddress, &SourceAddressLength, &ReceivedPacket);
        }

        else if (ClassInstance->IsSta)
        {
            ClassInstance->UdpPollingOnSta(&SourceAddress, &SourceAddressLength, &ReceivedPacket);
        }

        else
        {
            ;
        }

        vTaskDelay(ClassInstance->UdpPollingTaskCycleTime / portTICK_PERIOD_MS);
    }
}

// Function to poll for UDP data on the AP
void WifiClass::UdpPollingOnAp(struct sockaddr_in* SourceAddress, socklen_t* SourceAddressLength, UdpPacket* ReceivedPacket)
{
    ReceivedPacket->PacketLength = recvfrom(ClassInstance->UdpSocketFD, 
                                            ReceivedPacket->Data, 
                                            sizeof(ReceivedPacket->Data) - 1, 
                                            MSG_DONTWAIT, 
                                            (struct sockaddr*)SourceAddress, 
                                            SourceAddressLength);
    
    if (ReceivedPacket->PacketLength > 0)
    {            
        inet_ntop(AF_INET, &SourceAddress->sin_addr, ReceivedPacket->SenderIp, sizeof(ReceivedPacket->SenderIp));
        ReceivedPacket->SenderPort = ntohs(SourceAddress->sin_port);

        if ((xQueueSend(ClassInstance->UdpProcessingQueue, &ReceivedPacket, 0) != pdPASS) && (ClassInstance->IsRuntimeLoggingEnabled))
        {
            ESP_LOGE(TAG, "UDP Processing Queue full!");
        }

        memset(SourceAddress, 0, sizeof(struct sockaddr_in));
        memset(ReceivedPacket, 0, sizeof(UdpPacket));
    }
}

// Function to poll for UDP data on the STA
void WifiClass::UdpPollingOnSta(struct sockaddr_in* SourceAddress, socklen_t* SourceAddressLength, UdpPacket* ReceivedPacket)
{

    ReceivedPacket->PacketLength = recvfrom(ClassInstance->UdpSocketFD, 
                                            ReceivedPacket->Data, 
                                            sizeof(ReceivedPacket->Data) - 1, 
                                            MSG_DONTWAIT, 
                                            (struct sockaddr*)SourceAddress, 
                                            SourceAddressLength);

    if (ReceivedPacket->PacketLength > 0)
    {            
        inet_ntop(AF_INET, &SourceAddress->sin_addr, ReceivedPacket->SenderIp, sizeof(ReceivedPacket->SenderIp));
        ReceivedPacket->SenderPort = ntohs(SourceAddress->sin_port);

        if ((xQueueSend(ClassInstance->UdpProcessingQueue, &ReceivedPacket, 0) != pdPASS) && (ClassInstance->IsRuntimeLoggingEnabled))
        {
            ESP_LOGE(TAG, "UDP Processing Queue full!");
        }

        memset(SourceAddress, 0, sizeof(struct sockaddr_in));
        memset(ReceivedPacket, 0, sizeof(UdpPacket));
    }

    /**PacketLength = 0;

    *PacketLength = recvfrom(ClassInstance->UdpSocketFD, 
                            ClassInstance->UdpHostBuffer, 
                            sizeof(ClassInstance->UdpHostBuffer), 
                            MSG_DONTWAIT, 
                            (struct sockaddr*)&ClassInstance->UdpHostServerAddress, 
                            &ClassInstance->UdpAddressLength);

    if (*PacketLength > 0)
    {            
        if ((xQueueSend(ClassInstance->UdpProcessingQueue, PacketLength, 0) != pdPASS) and (ClassInstance->IsRuntimeLoggingEnabled))
        {
            ESP_LOGE(TAG, "UDP Processing Queue full!");
        }
    }*/
}





// Task that processes received UDP data
void WifiClass::UdpProcessingTask(void* pvParameters)
{
    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "UDP PROCESSING TASK BEGIN");
    }

    int PacketLength;
    UdpPacket* ReceivedPacket;

    while (true)
    {
        if (ClassInstance->IsAp)
        {
            if (xQueueReceive(ClassInstance->UdpProcessingQueue, &ReceivedPacket, portMAX_DELAY) == pdPASS)
            {
                ClassInstance->UdpProcessOnAp(ReceivedPacket);                
            }
        }

        else if (ClassInstance->IsSta)
        {
            if (xQueueReceive(ClassInstance->UdpProcessingQueue, &ReceivedPacket, portMAX_DELAY) == pdPASS)
            {
                ClassInstance->UdpProcessOnSta(ReceivedPacket);                
            }
        }

        else
        {
            ;
        }
    }
}

// Function to process UDP data on the AP
void WifiClass::UdpProcessOnAp(UdpPacket* ReceivedPacket)
{
    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "UDP PROCESSING PACKET");
        ESP_LOGI(TAG, "Processing UDP packet from %s:%d, length = %d",
                 ReceivedPacket->SenderIp, ReceivedPacket->SenderPort, ReceivedPacket->PacketLength);
        ESP_LOGI(TAG, "Received Data: %.*s", ReceivedPacket->PacketLength, ReceivedPacket->Data);          
    }

    // If Sync Packet
    if (strcmp((char*)ReceivedPacket->Data, "Sync") == 0)
    {
        // Find the device in ClientWifiDeviceList using the sender's IP
        auto deviceIt = std::find_if(ClassInstance->ClientWifiDeviceList.begin(), ClassInstance->ClientWifiDeviceList.end(),
            [&](const WifiDevice& device) {
                return strcmp(device.IpAddress, ReceivedPacket->SenderIp) == 0;
            });

        // If device is found
        if (deviceIt != ClassInstance->ClientWifiDeviceList.end())
        {
            deviceIt->IsSyncSystemRunning = true;
            if (deviceIt->SyncPacketCheckCounter <= 3)
            {
                deviceIt->SyncPacketCheckCounter = deviceIt->SyncPacketCheckCounter + 1;
            }
        }
        else
        {
            // Device not found in the list
            if (ClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGE(TAG, "Sync packet received from unknown device: %s", ReceivedPacket->SenderIp);
            }
        }
    }
}

// Function to process UDP data on the STA
void WifiClass::UdpProcessOnSta(UdpPacket* ReceivedPacket)
{
    // Start Processing
    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "UDP PROCESSING PACKET");
        ESP_LOGI(TAG, "Processing UDP packet from %s:%d, length = %d",
                 ReceivedPacket->SenderIp, ReceivedPacket->SenderPort, ReceivedPacket->PacketLength);
        ESP_LOGI(TAG, "Received Data: %.*s", ReceivedPacket->PacketLength, ReceivedPacket->Data);          
    }

    // If Sync Packet
    if (strcmp((char*)ReceivedPacket->Data, "Sync") == 0)
    {
        ClassInstance->HostWifiDevice.IsSyncSystemRunning = true;
        if (ClassInstance->HostWifiDevice.SyncPacketCheckCounter <= 3)
        {
            ClassInstance->HostWifiDevice.SyncPacketCheckCounter = ClassInstance->HostWifiDevice.SyncPacketCheckCounter + 1;
        }
    }
}





// Task that performs background UDP system functions, such as occasional system pings
void WifiClass::UdpSystemTask(void* pvParameters)
{
    if (ClassInstance->IsRuntimeLoggingEnabled)
    {
        printf("\n");
        ESP_LOGW(TAG, "UDP SYSTEM TASK BEGIN");
    }

    uint8_t Counter = 0;

    while(true)
    {
        if (Counter >= (1000 / ClassInstance->UdpSystemTaskCycleTime))
        {
            Counter = 0;
        }

        if (ClassInstance->IsAp)
        {
            ClassInstance->UdpSystemOnAp(Counter);
        }

        if (ClassInstance->IsSta)
        {
            ClassInstance->UdpSystemOnSta(Counter);
        }

        Counter ++;
        vTaskDelay(ClassInstance->UdpSystemTaskCycleTime / portTICK_PERIOD_MS);
    }
}

// Function to control UDP System on AP
void WifiClass::UdpSystemOnAp(uint8_t Counter)
{
    // Send Sync Packets
    if (Counter % (200 / UdpSystemTaskCycleTime) == 0)
    {
        if (ClassInstance->IsRuntimeLoggingEnabled)
        {    
            printf("\n");
            ESP_LOGW(TAG, "UDP AP SYNC SYSTEM");  
        }
        for (int i = 0; i < ClassInstance->ClientWifiDeviceList.size(); i++)
        {
            if (ClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGI(TAG, "Sending UDP Sync Packet to %s...", ClassInstance->ClientWifiDeviceList[i].IpAddress);
            }

            if (ClassInstance->SendUdpPacket("Sync", ClassInstance->ClientWifiDeviceList[i].IpAddress, 25000))
            {
                if (ClassInstance->IsRuntimeLoggingEnabled)
                {
                    ESP_LOGI(TAG, "Sent UDP Sync Packet!");
                }
            }
        }
    }

    // Measure Received Sync Packets
    if (Counter % (250 / UdpSystemTaskCycleTime) == 0)
    {
        for (int i = 0; i < ClassInstance->ClientWifiDeviceList.size(); i++)
        {
            if (ClassInstance->ClientWifiDeviceList[i].IsSyncSystemRunning)
            {
                ClassInstance->ClientWifiDeviceList[i].SyncPacketCheckCounter = ClassInstance->ClientWifiDeviceList[i].SyncPacketCheckCounter - 1; 

                if (ClassInstance->ClientWifiDeviceList[i].SyncPacketCheckCounter <= 0)
                {
                    printf("\n");
                    ESP_LOGE(TAG, "DEVICE TIMEOUT, DE-AUTHENTICATING");
                    ClassInstance->DeauthenticateDeviceAp(ClassInstance->ClientWifiDeviceList[i].IpAddress);
                }
            }

            else
            {
                if (ClassInstance->IsRuntimeLoggingEnabled)
                {
                    ESP_LOGI(TAG, "Sync system not running on %s", ClassInstance->ClientWifiDeviceList[i].IpAddress);
                }
            }
        }
    }
}

// Function to control UDP System on STA
void WifiClass::UdpSystemOnSta(uint8_t Counter)
{
    // Send Sync Packets
    if (Counter % (200 / UdpSystemTaskCycleTime) == 0)
    {
        if (ClassInstance->IsRuntimeLoggingEnabled)
        {
            printf("\n");
            ESP_LOGW(TAG, "UDP STA SYNC SYSTEM");
            ESP_LOGI(TAG, "Sending UDP Sync Packet to %s...", ClassInstance->AccessPointIp);
        }

        if (ClassInstance->SendUdpPacket("Sync", ClassInstance->HostWifiDevice.IpAddress, 25000))
        {
            if (ClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGI(TAG, "Sent UDP Sync Packet!");  
            }
        }
    }

    // Measure Received Sync Packets
    if (Counter % (250 / UdpSystemTaskCycleTime) == 0)
    {
        if (ClassInstance->HostWifiDevice.IsSyncSystemRunning)
        {
            ClassInstance->HostWifiDevice.SyncPacketCheckCounter = ClassInstance->HostWifiDevice.SyncPacketCheckCounter - 1; 

            if (ClassInstance->HostWifiDevice.SyncPacketCheckCounter <= 0)
            {
                printf("\n");
                ESP_LOGE(TAG, "DEVICE TIMEOUT, DE-AUTHENTICATING");
                ClassInstance->DeauthenticateDeviceSta(ClassInstance->HostWifiDevice.IpAddress);
            }
        }
        else
        {
            if (ClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGI(TAG, "Sync system not running on %s", ClassInstance->HostWifiDevice.IpAddress);
            }
        }
    }
}





// Function that sets up the system as an Access Point
bool WifiClass::SetupWifiAP(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse)
{
    printf("\n");
    ESP_LOGW(TAG, "SETUP WI-FI AP");
    IsAp = true;


    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return false; 
    }
    ESP_LOGI(TAG, "1 - NVS Ready!");


    // Initialize network interface
    ret = esp_netif_init();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize network interface: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "2 - Network Interface Ready!");


    // Create event loop
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "3 - Event Loop Ready!");


    // Set up the AP interface
    esp_netif_t* netif = esp_netif_create_default_wifi_ap();
    if (netif == nullptr) 
    {
        ESP_LOGE(TAG, "Failed to create default Wi-Fi AP interface");
        return false;
    }
    ESP_LOGI(TAG, "4 - AP Interface Ready!");


    // Initialize wifi stack with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi stack: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "5 - Wi-Fi Stack Ready!");


    // Set the country region before initializing Wi-Fi
    wifi_country_t country = 
    {
        .cc = "GB",        // UK country code
        .schan = 1,        // Start channel
        .nchan = 13,       // Number of channels available
        .max_tx_power = 20 // Max TX power (can vary based on the region)
    };
    ret = esp_wifi_set_country(&country);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to set country: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "6 - Country Set To GB!");


    // Register event handler
    ret = (esp_event_handler_instance_register(WIFI_EVENT,
                                                ESP_EVENT_ANY_ID,
                                                &WifiClass::WifiEventHandlerAp,
                                                ClassInstance,
                                                NULL));
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to register wifi event handler: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "7 - Wifi Event Handler Ready!");


    // Register event handler
    ret = esp_event_handler_instance_register(IP_EVENT,
                                                ESP_EVENT_ANY_ID,
                                                &WifiClass::IpEventHandlerAp,
                                                ClassInstance,
                                                NULL);
    if (ret != ESP_OK)
    {
    ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
    return false;       
    }
    ESP_LOGI(TAG, "8 - IP Event Handler Ready!");


    // Configure AP settings
    wifi_config_t wifi_config = 
    {
        .ap = 
        {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
    #ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .max_connection = MAX_STA_CONN,
            .pmf_cfg = 
            {
                .required = true,
            },
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
    #else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = MAX_STA_CONN,
            .pmf_cfg = 
            {
                .required = true,
            },
    #endif
        },
    };
    // Open access if no password specified
    if (strlen(WIFI_PASS) == 0) 
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    // Set the wifi mode to AP
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to set Wi-Fi mode: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "9 - Wi-Fi Mode Set To AP!");


    // Apply previously made configuration
    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to apply Wi-Fi configuration: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "10 - Wi-Fi Configuration Applied!");


    // Start wifi AP
    ret = esp_wifi_start();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to start Wi-Fi AP: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "11 - Wi-Fi AP Started!");


    ret = esp_wifi_set_inactive_time(WIFI_IF_AP, Timeout);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set Inactive Timer: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "12 - Inactive Timer Set To 10s!");


    if (!SetupUdpSocket(UdpPort))
    {
        ESP_LOGE(TAG, "Failed to bind UDP socket");
        return false;
    }
    ESP_LOGI(TAG, "13 - UDP Socket Bound!");


    UdpPollingTaskHandle = xTaskCreateStaticPinnedToCore
    (
        UdpPollingTask,                     // Task function
        "Udp Polling Task",                 // Task name
        UdpPollingTaskStackSize,            // Stack depth
        NULL,                               // Parameters to pass
        UdpPollingTaskPriority,             // Low priority
        UdpPollingTaskStack,                // Preallocated stack memory
        &UdpPollingTaskTCB,                 // Preallocated TCB memory
        CoreToUse                           // Core assigned
    );   
    if (UdpPollingTaskHandle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create UDP polling task");
        return false;
    }
    ESP_LOGI(TAG, "14 - UDP Polling Task Created!");


    UdpProcessingQueue = xQueueCreate(10, sizeof(UdpPacket*)); 
    if (UdpProcessingQueue == NULL) 
    {
        ESP_LOGE(TAG, "Failed to create UDP queue");
        return false;
    }
    ESP_LOGI(TAG, "15 - UdpProcessingQueue Ready!");


    UdpProcessingTaskHandle = xTaskCreateStaticPinnedToCore
    (
        UdpProcessingTask,                      // Task function
        "Udp Processing Task",                  // Task name
        UdpProcessingTaskStackSize,             // Stack depth
        NULL,                                   // Parameters to pass
        UdpProcessingTaskPriority,              // High priority
        UdpProcessingTaskStack,                 // Preallocated stack memory
        &UdpProcessingTaskTCB,                  // Preallocated TCB memory
        CoreToUse                               // Core assigned
    );   
    if (UdpProcessingTaskHandle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create UDP processing task");
        return false;
    }
    ESP_LOGI(TAG, "16 - UDP Processing Task Created!");


    UdpSystemTaskHandle = xTaskCreateStaticPinnedToCore
    (
        UdpSystemTask,                          // Task function
        "Udp System Task",                      // Task name
        UdpSystemTaskStackSize,                 // Stack depth
        NULL,                                   // Parameters to pass
        UdpSystemTaskPriority,                  // High priority
        UdpSystemTaskStack,                     // Preallocated stack memory
        &UdpSystemTaskTCB,                      // Preallocated TCB memory
        CoreToUse                               // Core assigned
    );   
    if (UdpProcessingTaskHandle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create UDP System task");
        return false;
    }
    ESP_LOGI(TAG, "17 - UDP System Task Created!");


    ESP_LOGI(TAG, "SSID: %s, Password: %s, Channel: %d",
             WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
    ESP_LOGI(TAG, "SETUP WI-FI AP SUCCESSFUL");    
    printf("\n");
    return true;
}

// Function that sets up the system as a Station
bool WifiClass::SetupWifiSta(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse)
{
    printf("\n");
    ESP_LOGW(TAG, "SETUP WI-FI STATION");
    IsSta = true;


    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
        return false; // Early return if the error occurs
    }
    ESP_LOGI(TAG, "1 - NVS Ready!");


    // Initialize network interface
    ret = esp_netif_init();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize network interface: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "2 - Network Interface Ready!");


    // Create event loop
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "3 - Event Loop Ready!");


    // Set up the Station interface
    esp_netif_t* netif = esp_netif_create_default_wifi_sta();
    if (netif == nullptr) 
    {
        ESP_LOGE(TAG, "Failed to create default Wi-Fi Station interface");
        return false;
    }
    ESP_LOGI(TAG, "4 - Station Interface Ready!");


    // Initialize Wi-Fi stack with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi stack: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "5 - Wi-Fi Stack Ready!");


    // Set the country region before initializing Wi-Fi
    wifi_country_t country = 
    {
        .cc = "GB",        // UK country code
        .schan = 1,        // Start channel
        .nchan = 13,       // Number of channels available
        .max_tx_power = 20 // Max TX power (can vary based on the region)
    };
    ret = esp_wifi_set_country(&country);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set country: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "6 - Country Set To GB!");


    // Register event handler
    ret = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              &WifiClass::WifiEventHandlerSta,
                                              ClassInstance,
                                              NULL);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "7 - Wifi Event Handler Ready!");


    // Register event handler
    ret = esp_event_handler_instance_register(IP_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              &WifiClass::IpEventHandlerSta,
                                              ClassInstance,
                                              NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
        return false;       
    }
    ESP_LOGI(TAG, "8 - IP Event Handler Ready!");


    // Configure Station settings
    wifi_config_t wifi_config = {};
    strcpy(reinterpret_cast<char*>(wifi_config.sta.ssid), WIFI_SSID);
    strcpy(reinterpret_cast<char*>(wifi_config.sta.password), WIFI_PASS);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;


    // Set the Wi-Fi mode to Station
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to set Wi-Fi mode: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "9 - Wi-Fi Mode Set To Station!");


    // Apply Wi-Fi configuration
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to apply Wi-Fi configuration: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "10 - Wi-Fi Configuration Applied!");


    // Start Wi-Fi in Station mode
    ret = esp_wifi_start();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to start Wi-Fi Station: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "11 - Wi-Fi Station Started!");


    ret = esp_wifi_set_inactive_time(WIFI_IF_STA, Timeout);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set Inactive Timer: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "12 - Inactive Timer Set To 10s!");


    if (!SetupUdpSocket(UdpPort))
    {
        ESP_LOGE(TAG, "Failed to bind UDP socket");
        return false;
    }
    ESP_LOGI(TAG, "13 - UDP Socket Bound!");


    UdpPollingTaskHandle = xTaskCreateStaticPinnedToCore
    (
        UdpPollingTask,                     // Task function
        "Udp Polling Task",                 // Task name
        UdpPollingTaskStackSize,            // Stack depth
        NULL,                               // Parameters to pass
        UdpPollingTaskPriority,             // Low priority
        UdpPollingTaskStack,                // Preallocated stack memory
        &UdpPollingTaskTCB,                 // Preallocated TCB memory
        CoreToUse                           // Core assigned
    );   
    if (UdpPollingTaskHandle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create UDP polling task");
        return false;
    }
    ESP_LOGI(TAG, "14 - UDP Polling Task Created!");


    UdpProcessingQueue = xQueueCreate(10, sizeof(int)); 
    if (UdpProcessingQueue == NULL) 
    {
        ESP_LOGE(TAG, "Failed to create UDP queue");
        return false;
    }
    ESP_LOGI(TAG, "15 - UdpProcessingQueue Ready!");


    UdpProcessingTaskHandle = xTaskCreateStaticPinnedToCore
    (
        UdpProcessingTask,                      // Task function
        "Udp Processing Task",                  // Task name
        UdpProcessingTaskStackSize,             // Stack depth
        NULL,                                   // Parameters to pass
        UdpProcessingTaskPriority,              // High priority
        UdpProcessingTaskStack,                 // Preallocated stack memory
        &UdpProcessingTaskTCB,                  // Preallocated TCB memory
        CoreToUse                               // Core assigned
    );   
    if (UdpProcessingTaskHandle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create UDP processing task");
        return false;
    }
    ESP_LOGI(TAG, "16 - UDP Processing Task Created!");


    UdpSystemTaskHandle = xTaskCreateStaticPinnedToCore
    (
        UdpSystemTask,                          // Task function
        "Udp System Task",                      // Task name
        UdpSystemTaskStackSize,                 // Stack depth
        NULL,                                   // Parameters to pass
        UdpSystemTaskPriority,               // High priority
        UdpSystemTaskStack,                     // Preallocated stack memory
        &UdpSystemTaskTCB,                      // Preallocated TCB memory
        CoreToUse                               // Core assigned
    );   
    if (UdpProcessingTaskHandle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create UDP System task");
        return false;
    }
    ESP_LOGI(TAG, "17 - UDP System Task Created!");


    ESP_LOGI(TAG, "Connecting to SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "SETUP WI-FI STATION SUCCESSFUL");     
    esp_wifi_disconnect();  // Disconnect first, just in case
    esp_wifi_connect();     // Manually trigger the connection
    printf("\n");
    return true;
}





//==============================================================================// 
//                                                                              //
//                       Public Setup Functions                                 //
//                                                                              //
//==============================================================================// 

// Function that sets up ESP-NOW
bool WifiClass::SetupEspNow(uint8_t CoreToUse)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupEspNow Executed!");


    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS Ready!");


    // Initialize ESP-NOW
    esp_err_t init_status = esp_now_init();
    if (init_status != ESP_OK) 
    {
        ESP_LOGE(TAG, "ESP-NOW initialization failed: %s", esp_err_to_name(init_status));
        return false;
    }
    ESP_LOGI(TAG, "ESP-Now Ready!");


    // Create a queue for handling ESP-NOW device registration
    EspNowDeviceQueue = xQueueCreate(10, sizeof(WifiDevice));
    if (EspNowDeviceQueue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create deviceQueue!");
        return false;
    }
    ESP_LOGI(TAG, "EspNowDeviceQueue Ready!");


    // Create a FreeRTOS task to handle ESP-NOW registration
    EspNowTaskHandle = xTaskCreateStaticPinnedToCore
    (
        EspNowTask, 
        "ESP-Now Task",
        EspNowTaskStackSize,
        ClassInstance,
        EspNowTaskPriority,
        EspNowTaskStack,
        &EspNowTaskTCB,
        CoreToUse
    );
    ESP_LOGI(TAG, "EspNowTask Ready!");

    
    ESP_LOGI(TAG, "SetupEspNow Successful!");
    printf("\n");
    return true;
}

// Function that sets up the wifi system based on the menu configuration
bool WifiClass::SetupWifi(uint8_t CoreToUse)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupWifi Executed!");


    // AP
    if (WIFI_MODE == 0)
    {
        if (!SetupWifiAP(UDP_PORT, 10, CoreToUse))
        {
            ESP_LOGI(TAG, "SetupWifiAP Failed!");
            return false;
        }
    }


    // Station
    if (WIFI_MODE == 1)
    {
        if (!SetupWifiSta(UDP_PORT, 10, CoreToUse))
        {
            ESP_LOGI(TAG, "SetupWifiSta Failed!");
            return false;
        }
    }


    ESP_LOGI(TAG, "SetupWifi Successful!");
    printf("\n");
    return true;
}





//==============================================================================// 
//                                                                              //
//                             Commands                                         //
//                                                                              //
//==============================================================================//

// Function that sends a message as a UDP packet to a destination
bool WifiClass::SendUdpPacket(const char* Data, const char* DestinationIP, uint16_t DestinationPort)
{
    if ((IsAp and (GetNumClientsConnected() == 0)) or
         (IsSta and !GetIsConnectedToHost()))
    {
        if (IsRuntimeLoggingEnabled)
        {
            ESP_LOGE(TAG, "Not Connected");
        }
        return false;
    }

    struct sockaddr_in DestinationAddress;

    memset(&DestinationAddress, 0, sizeof(DestinationAddress));

    DestinationAddress.sin_family = AF_INET;

    if (inet_pton(AF_INET, DestinationIP, &DestinationAddress.sin_addr) <= 0)
    {
        if (IsRuntimeLoggingEnabled)
        {
            ESP_LOGE(TAG, "Invalid IP address: %s", DestinationIP);
        }
        return false;
    }

    DestinationAddress.sin_port = htons(DestinationPort);

    int SentBytes = sendto(UdpSocketFD, Data, strlen(Data), 0, (struct sockaddr*)&DestinationAddress, sizeof(DestinationAddress));

    if (SentBytes < 0)
    {
        if (IsRuntimeLoggingEnabled)
        {
            ESP_LOGE(TAG, "Failed to send UDP packet, error = %d", errno);
        }
        return false;
    }
    return true;
}





//==============================================================================// 
//                                                                              //
//                             Get / Set                                        //
//                                                                              //
//==============================================================================//

size_t WifiClass::GetNumClientsConnected()
{
    return ClientWifiDeviceList.size();
}

bool WifiClass::GetIsConnectedToHost()
{
    return IsConnectedToAP;
}

bool WifiClass::GetIsAp()
{
    return IsAp;
}

bool WifiClass::GetIsSta()
{
    return IsSta;
}

const char* WifiClass::GetApIpAddress()
{
    return AccessPointIp;
}

TaskHandle_t WifiClass::GetEspNowTaskHandle()
{
    return EspNowTaskHandle;
}

TaskHandle_t WifiClass::GetUdpPollingTaskHandle()
{
    return UdpPollingTaskHandle;
}

TaskHandle_t WifiClass::GetUdpProcessingTaskHandle()
{
    return UdpProcessingTaskHandle;
}

void WifiClass::SetRuntimeLogging(bool EnableRuntimeLogging)
{
    IsRuntimeLoggingEnabled = EnableRuntimeLogging;
}





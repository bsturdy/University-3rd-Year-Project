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
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
   
        ClientDevice NewDevice;
        NewDevice.TimeOfConnection = esp_timer_get_time();
        memcpy(NewDevice.MacId, event->mac, sizeof(NewDevice.MacId));
        NewDevice.IsRegisteredWithEspNow = false;
        ClassInstance->DeviceList.push_back(NewDevice);

        BaseType_t higherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(ClassInstance->EspNowDeviceQueue, &NewDevice, &higherPriorityTaskWoken);
    } 
    
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) 
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);

        auto it = std::find_if(ClassInstance->DeviceList.begin(), ClassInstance->DeviceList.end(),
                                [event](const ClientDevice& device)
                                {
                                    return memcmp(device.MacId, event->mac, sizeof(device.MacId)) == 0;
                                });
        
        if (it != ClassInstance->DeviceList.end()) 
        {
            // Device found, remove it
            ClientDevice TempDevice = *it;

            TempDevice.IsRegisteredWithEspNow = true;

            ClassInstance->DeviceList.erase(it);  

            BaseType_t higherPriorityTaskWoken = pdFALSE;

            xQueueSendFromISR(ClassInstance->EspNowDeviceQueue, &TempDevice, &higherPriorityTaskWoken);
        }
    }
}

// Software event handler for wifi STA events
void WifiClass::WifiEventHandlerSta(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        ClassInstance->IsConnectedToAP = false;
        //if (ClassInstance->AccessPointIp != NULL)
        //{
        //    memset(&ClassInstance->AccessPointIp, 0, sizeof(ClassInstance->AccessPointIp));
        //}
        ESP_LOGI(TAG, "Disconnected from Wi-Fi, retrying...");
        esp_wifi_disconnect();
        esp_wifi_connect();  // Retry connection immediately
    } 

    else if (event_id == WIFI_EVENT_STA_CONNECTED) 
    {
        ClassInstance->IsConnectedToAP = true;
        //inet_ntoa_r(AccessPointIP.ip, ClassInstance->AccessPointIp, sizeof(ClassInstance->AccessPointIp));
        ESP_LOGI(TAG, "Connected to Wi-Fi!");
    }
}

// Software event handler for IP AP events
void WifiClass::IpEventHandlerAp(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_AP_STAIPASSIGNED)
    {
        ip_event_ap_staipassigned_t* event = (ip_event_ap_staipassigned_t*) event_data;
        
        // Convert the assigned IP to a string
        char ip_str[16];
        esp_ip4addr_ntoa(&event->ip, ip_str, sizeof(ip_str));

        ESP_LOGI(TAG, "Station assigned IP: %s", ip_str);

        // Loop through the DeviceList and find the device by MAC address
        for (auto& device : ClassInstance->DeviceList)
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
        esp_netif_t *NetIf = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t TempAccessPointIp;

        // Retrieve IP address
        esp_netif_get_ip_info(NetIf, &TempAccessPointIp);
        esp_ip4addr_ntoa(&TempAccessPointIp.gw, ClassInstance->AccessPointIp, sizeof(ClassInstance->AccessPointIp));
        ESP_LOGI(TAG, "Access Point IP: %s", ClassInstance->AccessPointIp);
    }
}

// Task that connects and disconnects devices from ESP-NOW registery
void WifiClass::EspNowTask(void *pvParameters)
{
    ClientDevice DeviceToAction;

    while(true)
    {
        if ((xQueueReceive(ClassInstance->EspNowDeviceQueue, &DeviceToAction, portMAX_DELAY) == pdPASS))
        {
            printf("\n");
            ESP_LOGI(TAG, "EspNowTask Executed!");

            if (!DeviceToAction.IsRegisteredWithEspNow)
            {
                ESP_LOGI(TAG, "Processing new device registration in ESP-NOW task for MAC: " 
                        MACSTR, MAC2STR(DeviceToAction.MacId));

                if (!ClassInstance->EspNowRegisterDevice(&DeviceToAction))
                {
                    ESP_LOGE(TAG, "Failed to register device with ESP-NOW");
                }
            }

            else if (DeviceToAction.IsRegisteredWithEspNow)
            {
                ESP_LOGI(TAG, "Deleting device registration in ESP-NOW task for MAC: " 
                        MACSTR, MAC2STR(DeviceToAction.MacId));

                if (!ClassInstance->EspNowDeleteDevice(&DeviceToAction))
                {
                    ESP_LOGE(TAG, "Failed to Delete device with ESP-NOW");
                }
            }

            ESP_LOGI(TAG, "EspNowTask Successful!");
            printf("\n");
        }
    }
}

// Function that registers device with ESP-NOW
bool WifiClass::EspNowRegisterDevice(ClientDevice* DeviceToRegister)
{
    printf("\n");
    ESP_LOGI(TAG, "EspNowRegisterDevice Executed!");

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

    ESP_LOGI(TAG, "Registering device with MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
            peerMacAddr[0], peerMacAddr[1], peerMacAddr[2],
            peerMacAddr[3], peerMacAddr[4], peerMacAddr[5]);

    memcpy(peerInfo.peer_addr, peerMacAddr, 6);    

    peerInfo.channel = 0;  // Use the default channel (or specify a specific channel)
    peerInfo.encrypt = false;  // Disable encryption (set to true for encryption)

    esp_err_t add_peer_status = esp_now_add_peer(&peerInfo);
    if (add_peer_status != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to add peer: %s", esp_err_to_name(add_peer_status));
        return false;
    }

    DeviceToRegister->IsRegisteredWithEspNow = true;

    ESP_LOGI(TAG, "EspNowRegisterDevice Successful!");
    printf("\n");
    return true;
}

// Function that deregisters device with ESP-NOW
bool WifiClass::EspNowDeleteDevice(ClientDevice* DeviceToDelete)
{
    printf("\n");
    ESP_LOGI(TAG, "EspNowDeleteDevice Executed!");

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

    ESP_LOGI(TAG, "Deleting device with MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
            peerMacAddr[0], peerMacAddr[1], peerMacAddr[2],
            peerMacAddr[3], peerMacAddr[4], peerMacAddr[5]);

    // Call the ESP-NOW deletion function.
    esp_err_t del_peer_status = esp_now_del_peer(peerMacAddr);
    if (del_peer_status != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to delete peer: %s", esp_err_to_name(del_peer_status));
        return false;
    }

    DeviceToDelete->IsRegisteredWithEspNow = false;

    ESP_LOGI(TAG, "EspNowDeleteDevice Successful!");
    printf("\n");
    return true;
}

// Function that sets up a UDP socket
bool WifiClass::SetupUdpSocket(uint16_t UdpPort)
{
    UdpSocketFD = socket(AF_INET, SOCK_DGRAM, 0);
    if (UdpSocketFD < 0)
    {
        ESP_LOGE(TAG, "Failed to create UDP socket");
        return false; 
    }

    memset(&UdpServerAddress, 0, sizeof(UdpServerAddress));
    UdpServerAddress.sin_family = AF_INET;
    UdpServerAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    UdpServerAddress.sin_port = htons(UdpPort);

    if (bind(UdpSocketFD, (struct sockaddr *)&UdpServerAddress, sizeof(UdpServerAddress)) < 0) 
    {
        ESP_LOGE(TAG, "Failed to bind UDP socket");
        close(UdpSocketFD);
        return false;
    }

    ESP_LOGI(TAG, "UDP Socket bound to port %d", UdpPort);
    return true;
}

// Task that polls for new UDP data received
void WifiClass::UdpPollingTask(void* pvParameters)
{
    while(true)
    {
        int PacketLength = recvfrom(ClassInstance->UdpSocketFD, ClassInstance->UdpBuffer, sizeof(ClassInstance->UdpBuffer), MSG_DONTWAIT, 
                            (struct sockaddr*)&ClassInstance->UdpServerAddress, &ClassInstance->UdpAddressLength);
        
        if (PacketLength > 0)
        {            
            if ((xQueueSend(ClassInstance->UdpProcessingQueue, &PacketLength, 0) != pdPASS) and (ClassInstance->IsRuntimeLoggingEnabled))
            {
                ESP_LOGE(TAG, "UDP Processing Queue full!");
            }
        }

        vTaskDelay(ClassInstance->UdpPollingTaskCycleTime / portTICK_PERIOD_MS);
    }
}

// Task that processes received UDP data
void WifiClass::UdpProcessingTask(void* pvParameters)
{
    int PacketLength;

    while (true)
    {
        if (xQueueReceive(ClassInstance->UdpProcessingQueue, &PacketLength, portMAX_DELAY) == pdPASS)
        {
            if (ClassInstance->IsRuntimeLoggingEnabled)
            {
                ESP_LOGI(TAG, "Processing UDP packet, length = %d", PacketLength);
            }

            if (PacketLength > sizeof(ClassInstance->UdpBuffer))
            {
                PacketLength = sizeof(ClassInstance->UdpBuffer);
            }

            if (strcmp((char*)ClassInstance->UdpBuffer, "Sync") == 0)
            {
                ClassInstance->SyncPacketCheckCounter = ClassInstance->SyncPacketCheckCounter + 1; 
                
                if (ClassInstance->IsRuntimeLoggingEnabled)
                {
                    ESP_LOGI(TAG, "Received Sync packet! Packet check: %i", ClassInstance->SyncPacketCheckCounter);
                }
            }

            if (ClassInstance->IsRuntimeLoggingEnabled)
            {
                for (int i = 0; i < PacketLength; i++)
                {
                    char ReceivedChar = ClassInstance->UdpBuffer[i];
                    ESP_LOGI(TAG, "%c", ReceivedChar);
                }
            }
            
        }
    }
}

// Task that performs background UDP system functions, such as occasional system pings
void WifiClass::UdpSystemTask(void* pvParameters)
{
    uint8_t Counter = 0;
    while(true)
    {
        if (Counter >= 10)
        {
            Counter = 0;
        }

        if (ClassInstance->IsAp)
        {
            // Sync System

            if (Counter % 2 == 0)
            {
                for (int i = 0; i < ClassInstance->DeviceList.size(); i++)
                {
                    if (ClassInstance->IsRuntimeLoggingEnabled)
                    {
                        ESP_LOGI(TAG, "Sending UDP Sync Packet to %s...", ClassInstance->DeviceList[i].IpAddress);
                    }

                    if (ClassInstance->SendUdpPacket("Sync", ClassInstance->DeviceList[i].IpAddress, 25000))
                    {
                        if (ClassInstance->IsRuntimeLoggingEnabled)
                        {
                            ESP_LOGI(TAG, "Sent UDP Sync Packet!");
                        }
                    }
                }
            }
        }

        if (ClassInstance->IsSta)
        {
            // Sync System

            if (Counter % 2 == 0)
            {
                if (ClassInstance->IsRuntimeLoggingEnabled)
                {
                    ESP_LOGI(TAG, "Sending UDP Sync Packet to %s...", ClassInstance->AccessPointIp);
                }

                if (ClassInstance->SendUdpPacket("Sync", ClassInstance->AccessPointIp, 25000))
                {
                    if (ClassInstance->IsRuntimeLoggingEnabled)
                    {
                        ESP_LOGI(TAG, "Sent UDP Sync Packet!");  
                    }
                }
            }
        }

        Counter ++;
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

// Function that sets up the system as an Access Point
bool WifiClass::SetupWifiAP(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupWifiAP Executed!");


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
        1,                                  // Low priority
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
        configMAX_PRIORITIES - 5,               // High priority
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
        configMAX_PRIORITIES - 5,               // High priority
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


    IsAp = true;
    ESP_LOGI(TAG, "SSID: %s, Password: %s, Channel: %d",
             WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
    ESP_LOGI(TAG, "SetupWifiAP Successful!");    
    printf("\n");
    return true;
}

// Function that sets up the system as a Station
bool WifiClass::SetupWifiSta(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse)
{
    printf("\n");
    ESP_LOGI(TAG, "SetupWifiSta Executed!");


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
        1,                                  // Low priority
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
        configMAX_PRIORITIES - 5,               // High priority
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
        configMAX_PRIORITIES - 5,               // High priority
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


    IsSta = true;
    ESP_LOGI(TAG, "Connecting to SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "SetupWifiSta Successful!");    
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
    EspNowDeviceQueue = xQueueCreate(10, sizeof(ClientDevice));
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
        5,
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
    return DeviceList.size();
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



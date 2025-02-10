#include "WifiClass.h"



//=============================================================================// 
//                            Wifi Class                                       //
//=============================================================================// 

#define WIFI_SSID           CONFIG_ESP_WIFI_SSID
#define WIFI_PASS           CONFIG_ESP_WIFI_PASSWORD
#define WIFI_CHANNEL        CONFIG_ESP_WIFI_CHANNEL
#define MAX_STA_CONN        CONFIG_ESP_MAX_STA_CONN

static WifiClass* ClassInstance;

static const char *TAG = "Wifi Class";

TaskHandle_t EspNowTaskHandle = NULL;
StackType_t WifiClass::EspNowTaskStack[EspNowStackSize];
StaticTask_t WifiClass::EspNowTaskTCB;

TaskHandle_t UdpPollingTaskHandle = NULL;
StackType_t WifiClass::UdpPollingTaskStack[UdpPollingTaskStackSize];
StaticTask_t WifiClass::UdpPollingTaskTCB;

TaskHandle_t UdpProcessingTaskHandle = NULL;
StackType_t WifiClass::UdpProcessingTaskStack[UdpProcessingTaskStackSize];
StaticTask_t WifiClass::UdpProcessingTaskTCB;



//=============================================================================//
//            Constructors, Destructors, Internal Functions                    //
//=============================================================================// 

WifiClass::WifiClass()
{
    ClassInstance = this;
    EspNowDeviceQueue = NULL;
}

WifiClass::~WifiClass()
{
    ;
}

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

            //===========================//
            //       FIX THIS            //
            //===========================//
            TempDevice.IsRegisteredWithEspNow = true;

            ClassInstance->DeviceList.erase(it);  

            BaseType_t higherPriorityTaskWoken = pdFALSE;

            xQueueSendFromISR(ClassInstance->EspNowDeviceQueue, &TempDevice, &higherPriorityTaskWoken);
        }
    }
}

void WifiClass::WifiEventHandlerSta(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)

{
    if (event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        ClassInstance->IsConnectedToAP = false;
        ESP_LOGI(TAG, "Disconnected from Wi-Fi, retrying...");
        esp_wifi_disconnect();
        esp_wifi_connect();  // Retry connection immediately
    } 

    else if (event_id == WIFI_EVENT_STA_CONNECTED) 
    {
        ClassInstance->IsConnectedToAP = true;
        ESP_LOGI(TAG, "Connected to Wi-Fi!");
    }
}


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

bool WifiClass::SetupUdpSocket(uint16_t UDP_PORT)
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
    UdpServerAddress.sin_port = htons(UDP_PORT);

    if (bind(UdpSocketFD, (struct sockaddr *)&UdpServerAddress, sizeof(UdpServerAddress)) < 0) 
    {
        ESP_LOGE(TAG, "Failed to bind UDP socket");
        close(UdpSocketFD);
        return false;
    }

    ESP_LOGI(TAG, "UDP Socket bound to port %d", UDP_PORT);
    return true;
}

void WifiClass::UdpPollingTask(void* pvParameters)
{
    while(true)
    {
        int PacketLength = recvfrom(ClassInstance->UdpSocketFD, ClassInstance->UdpBuffer, sizeof(ClassInstance->UdpBuffer), MSG_DONTWAIT, 
                            (struct sockaddr*)&ClassInstance->UdpServerAddress, &ClassInstance->UdpAddressLength);
        
        if (PacketLength > 0)
        {            
            if (xQueueSend(ClassInstance->UdpProcessingQueue, &PacketLength, 0) != pdPASS)
            {
                ESP_LOGE(TAG, "UDP Processing Queue full!");
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void WifiClass::UdpProcessingTask(void* pvParameters)
{
    int PacketLength;

    while (true)
    {
        if (xQueueReceive(ClassInstance->UdpProcessingQueue, &PacketLength, portMAX_DELAY) == pdPASS)
        {
            ESP_LOGI(TAG, "Processing UDP packet, length = %d", PacketLength);

            if (PacketLength > sizeof(ClassInstance->UdpBuffer))
            {
                PacketLength = sizeof(ClassInstance->UdpBuffer);
            }

            for (int i = 0; i < PacketLength; i++)
            {
                char ReceivedChar = ClassInstance->UdpBuffer[i];
                ESP_LOGI(TAG, "%c", ReceivedChar);
            }
        }
    }
}

//=============================================================================// 
//                           Setup Functions                                   //
//=============================================================================// 

bool WifiClass::SetupWifiAP(uint16_t UdpPort, uint16_t Timeout)
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
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "7 - Event Handler Ready!");


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
    ESP_LOGI(TAG, "8 - Wi-Fi Mode Set To AP!");


    // Apply previously made configuration
    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to apply Wi-Fi configuration: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "9 - Wi-Fi Configuration Applied!");


    // Start wifi AP
    ret = esp_wifi_start();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to start Wi-Fi AP: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "10 - Wi-Fi AP Started!");


    ret = esp_wifi_set_inactive_time(WIFI_IF_AP, Timeout);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set Inactive Timer: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "11 - Inactive Timer Set To 10s!");


    if (!SetupUdpSocket(UdpPort))
    {
        ESP_LOGE(TAG, "Failed to bind UDP socket");
        return false;
    }
    ESP_LOGI(TAG, "12 - UDP Socket Bound!");


    UdpPollingTaskHandle = xTaskCreateStatic
    (
        UdpPollingTask,                     // Task function
        "Udp Polling Task",                 // Task name
        UdpPollingTaskStackSize,            // Stack depth
        NULL,                               // Parameters to pass
        1,                                  // Low priority
        UdpPollingTaskStack,                // Preallocated stack memory
        &UdpPollingTaskTCB                  // Preallocated TCB memory
    );   
    if (UdpPollingTaskHandle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create UDP polling task");
        return false;
    }
    ESP_LOGI(TAG, "13 - UDP Polling Task Created!");


    UdpProcessingQueue = xQueueCreate(10, sizeof(int)); 
    if (UdpProcessingQueue == NULL) 
    {
        ESP_LOGE(TAG, "Failed to create UDP queue");
        return false;
    }
    ESP_LOGI(TAG, "14 - UdpProcessingQueue Ready!");


    UdpProcessingTaskHandle = xTaskCreateStatic
    (
        UdpProcessingTask,                      // Task function
        "Udp Processing Task",                  // Task name
        UdpProcessingTaskStackSize,             // Stack depth
        NULL,                                   // Parameters to pass
        configMAX_PRIORITIES - 5,               // High priority
        UdpProcessingTaskStack,                 // Preallocated stack memory
        &UdpProcessingTaskTCB                   // Preallocated TCB memory
    );   
    if (UdpProcessingTaskHandle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create UDP processing task");
        return false;
    }
    ESP_LOGI(TAG, "15 - UDP Processing Task Created!");


    ESP_LOGI(TAG, "SSID: %s, Password: %s, Channel: %d",
             WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
    ESP_LOGI(TAG, "SetupWifiAP Successful!");    
    printf("\n");
    return true;
}

bool WifiClass::SetupWifiSta(uint16_t UdpPort)
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
    ESP_LOGI(TAG, "7 - Event Handler Ready!");


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
    ESP_LOGI(TAG, "8 - Wi-Fi Mode Set To Station!");


    // Apply Wi-Fi configuration
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to apply Wi-Fi configuration: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "9 - Wi-Fi Configuration Applied!");


    // Start Wi-Fi in Station mode
    ret = esp_wifi_start();
    if (ret != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to start Wi-Fi Station: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "10 - Wi-Fi Station Started!");


    ret = esp_wifi_set_inactive_time(WIFI_IF_AP, 3);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set Inactive Timer: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "11 - Inactive Timer Set To 10s!");


    if (!SetupUdpSocket(UdpPort))
    {
        ESP_LOGE(TAG, "Failed to bind UDP socket");
        return false;
    }
    ESP_LOGI(TAG, "12 - UDP Socket Bound!");


    ESP_LOGI(TAG, "Connecting to SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "SetupWifiSta Successful!");    


    esp_wifi_disconnect();  // Disconnect first, just in case
    esp_wifi_connect();     // Manually trigger the connection
    printf("\n");
    return true;
}

bool WifiClass::SetupEspNow()
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
    EspNowTaskHandle = xTaskCreateStatic
    (
        EspNowTask, 
        "ESP-Now Task",
        EspNowStackSize,
        ClassInstance,
        5,
        EspNowTaskStack,
        &EspNowTaskTCB
    );
    ESP_LOGI(TAG, "EspNowTask Ready!");

    ESP_LOGI(TAG, "SetupEspNow Successful!");
    printf("\n");
    return true;
}



//=============================================================================// 
//                             Get / Set                                       //
//=============================================================================//

size_t WifiClass::GetNumClientsConnected()
{
    return DeviceList.size();
}

bool WifiClass::GetIsConnectedToHost()
{
    return IsConnectedToAP;
}
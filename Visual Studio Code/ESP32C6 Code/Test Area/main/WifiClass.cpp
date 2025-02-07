#include "WifiClass.h"



//=============================================================================// 
//                            Wifi Class                                       //
//=============================================================================// 

#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

static WifiClass* ClassInstance;

static const char *TAG = "Wifi Class";

TaskHandle_t EspNowTaskHandle = NULL;
StackType_t WifiClass::EspNowTaskStack[EspNowStackSize];
StaticTask_t WifiClass::EspNowTaskTCB;



//=============================================================================//
//            Constructors, Destructors, Internal Functions                    //
//=============================================================================// 

WifiClass::WifiClass()
{
    ClassInstance = this;
    DeviceQueue = NULL;
}

WifiClass::~WifiClass()
{
    ;
}

void WifiClass::wifi_event_handler(void* arg, esp_event_base_t event_base,
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
        xQueueSendFromISR(ClassInstance->DeviceQueue, &NewDevice, &higherPriorityTaskWoken);
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

            xQueueSendFromISR(ClassInstance->DeviceQueue, &TempDevice, &higherPriorityTaskWoken);
        }
    }
}

void WifiClass::EspNowTask(void *pvParameters)
{
    ClientDevice DeviceToAction;

    while(true)
    {
        if ((xQueueReceive(ClassInstance->DeviceQueue, &DeviceToAction, portMAX_DELAY) == pdPASS))
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



//=============================================================================// 
//                           Setup Functions                                   //
//=============================================================================// 

bool WifiClass::SetupWifiAP()
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
    ESP_ERROR_CHECK(ret);

    // Initialize network interface
    ESP_ERROR_CHECK(esp_netif_init());

    // Create event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Set up the AP interface
    esp_netif_create_default_wifi_ap();

    // Initialize wifi stack with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Set the country region before initializing Wi-Fi
    wifi_country_t country = {
        .cc = "GB",        // UK country code
        .schan = 1,        // Start channel
        .nchan = 13,       // Number of channels available
        .max_tx_power = 20 // Max TX power (can vary based on the region)
    };
    esp_err_t ret2 = esp_wifi_set_country(&country);
    if (ret2 != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to set country: %s", esp_err_to_name(ret2));
    }

    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiClass::wifi_event_handler,
                                                        ClassInstance,
                                                        NULL));
    // Configure AP settings
    wifi_config_t wifi_config = 
    {
        .ap = 
        {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
    #ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            .authmode = WIFI_AUTH_WPA3_PSK,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .pmf_cfg = 
            {
                .required = true,
            },
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
    #else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .pmf_cfg = 
            {
                .required = true,
            },
    #endif
        },
    };

    // Open access if no password specified
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) 
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    // Set the wifi mode to AP
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    // Apply previously made configuration
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    // Start wifi AP
    ESP_ERROR_CHECK(esp_wifi_start());

    // Log the AP
    ESP_LOGI(TAG, "SetupWifiAP Successful! SSID: %s, Password: %s, Channel: %d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
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
    DeviceQueue = xQueueCreate(10, sizeof(ClientDevice));
    if (DeviceQueue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create deviceQueue!");
        return false;
    }
    ESP_LOGI(TAG, "DeviceQueue Ready!");

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



//=============================================================================// 
//                             Get / Set                                       //
//=============================================================================//

size_t WifiClass::GetNumClientsConnected()
{
    return DeviceList.size();
}
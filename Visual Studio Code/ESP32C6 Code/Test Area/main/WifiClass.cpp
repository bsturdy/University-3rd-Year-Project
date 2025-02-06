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



//=============================================================================//
//            Constructors, Destructors, Internal Functions                    //
//=============================================================================// 

WifiClass::WifiClass()
{
    ClassInstance = this;
}

WifiClass::~WifiClass()
{
    ;
}

void WifiClass::wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    WifiClass* Instance = static_cast<WifiClass*>(arg);

    if (event_id == WIFI_EVENT_AP_STACONNECTED) 
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
   
        ClientDevice NewDevice;
        NewDevice.TimeOfConnection = esp_timer_get_time();
        memcpy(NewDevice.MacId, event->mac, sizeof(NewDevice.MacId));
        Instance->DeviceList.push_back(NewDevice);
    } 
    
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) 
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);

        auto it = std::find_if(Instance->DeviceList.begin(), Instance->DeviceList.end(),
                                [event](const ClientDevice& device)
                                {
                                    return memcmp(device.MacId, event->mac, sizeof(device.MacId)) == 0;
                                });
        if (it != Instance->DeviceList.end()) 
        {
            // Device found, remove it
            Instance->DeviceList.erase(it);  
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

    // Initialize ESP-NOW
    esp_err_t init_status = esp_now_init();
    if (init_status != ESP_OK) 
    {
        ESP_LOGE(TAG, "ESP-NOW initialization failed: %s", esp_err_to_name(init_status));
        return false;
    }

    ESP_LOGI(TAG, "SetupEspNow Successful!");
    printf("\n");
    return true;
}

bool WifiClass::EspNowRegisterDevice(ClientDevice DeviceConnected)
{
    printf("\n");
    ESP_LOGI(TAG, "EspNowRegisterDevice Executed!");

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));

    uint8_t peerMacAddr[] = 
    {
        DeviceConnected.MacId[0], 
        DeviceConnected.MacId[1],
        DeviceConnected.MacId[2],
        DeviceConnected.MacId[3],
        DeviceConnected.MacId[4],
        DeviceConnected.MacId[5]
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

    ESP_LOGI(TAG, "EspNowRegisterDevice Successful!");
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
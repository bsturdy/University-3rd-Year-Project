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
//                            Wifi Class                                        //
//                                                                              //
//==============================================================================// 

// #define WIFI_SSID                   CONFIG_ESP_WIFI_SSID
// #define WIFI_PASS                   CONFIG_ESP_WIFI_PASSWORD
// #define WIFI_CHANNEL                CONFIG_ESP_WIFI_CHANNEL
// #define MAX_STA_CONN                CONFIG_ESP_MAX_STA_CONN
// #define WIFI_MODE                   CONFIG_ESP_DEVICE_MODE
// #define UDP_PORT                    CONFIG_ESP_UDP_PORT

// #define EspNowTaskPriority          24
// #define UdpPollingTaskPriority      1
// #define UdpProcessingTaskPriority   23
// #define UdpSystemTaskPriority       22

// #define TAG                         "Wifi Class"

// static WifiClass* ClassInstance;






// //==============================================================================//
// //                                                                              //
// //            Constructors, Destructors, Internal Functions                     //
// //                                                                              //
// //==============================================================================// 

// // Constructor
// WifiClass::WifiClass()
// {
//     ClassInstance = this;
//     memset(&HostWifiDevice, 0, sizeof(HostWifiDevice));
// }

// // Destructor (Unsused, this class should exist throughout runtime)
// WifiClass::~WifiClass()
// {
//     ;
// }





// // Software event handler for wifi AP events
// void WifiClass::WifiEventHandlerAp(void* arg, esp_event_base_t event_base,
//                                     int32_t event_id, void* event_data)
// {
//     if (event_id == WIFI_EVENT_AP_STACONNECTED) 
//     {
//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             printf("\n");
//             ESP_LOGW(TAG, "WI-FI AP CONNECTION EVENT");
//         }


//         // Store event data
//         wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
//         ESP_LOGI(TAG, "Station "MACSTR" join, AID = %d",
//                  MAC2STR(event->mac), event->aid);


//         // Create wifi device with event data         
//         WifiDevice NewDevice;
//         NewDevice.TimeOfConnection = esp_timer_get_time();
//         memcpy(NewDevice.MacId, event->mac, sizeof(NewDevice.MacId));
//         //NewDevice.IsRegisteredWithEspNow = false;
//         NewDevice.aid = event->aid;


//         // Store device in class
//         ClassInstance->ClientWifiDeviceList.push_back(NewDevice);


//         // Trigger ESP-NOW task queue to add to ESP-NOW register
//         BaseType_t higherPriorityTaskWoken = pdFALSE;
       


//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             ESP_LOGW(TAG, "WI-FI AP CONNECTION EVENT COMPLETED");
//             printf("\n");
//         }
//     } 
    
//     else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) 
//     {
//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             printf("\n");
//             ESP_LOGW(TAG, "WI-FI AP DISCONNECTION EVENT");
//         }


//         // Store event data
//         wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
//         ESP_LOGI(TAG, "Station "MACSTR" leave, AID = %d, reason = %d",
//                  MAC2STR(event->mac), event->aid, event->reason);


//         // Find device to remove by MacId
//         auto it = std::find_if(ClassInstance->ClientWifiDeviceList.begin(), ClassInstance->ClientWifiDeviceList.end(),
//                                 [event](const WifiDevice& device)
//                                 {
//                                     return memcmp(device.MacId, event->mac, sizeof(device.MacId)) == 0;
//                                 });
        

//         if (it != ClassInstance->ClientWifiDeviceList.end()) 
//         {
//             // Copy wifi device data
//             WifiDevice TempDevice = *it;

//             // Wipe wifi device from the class
//             ClassInstance->ClientWifiDeviceList.erase(it); 

//             // Force all disconnected devices to attempt to deauth ESP-NOW
//             //TempDevice.IsRegisteredWithEspNow = true; 

//             // Trigger ESP-NOW task queue to wipe from ESP-NOW register
//             BaseType_t higherPriorityTaskWoken = pdFALSE;
            
//         }


//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             ESP_LOGW(TAG, "WI-FI AP DISCONNECTION EVENT COMPLETED");
//             printf("\n");
//         }  
//     }
// }

// // Software event handler for wifi STA events
// void WifiClass::WifiEventHandlerSta(void* arg, esp_event_base_t event_base,
//                                     int32_t event_id, void* event_data)
// {
//     if (event_id == WIFI_EVENT_STA_CONNECTED) 
//     {
//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             printf("\n");
//             ESP_LOGW(TAG, "WI-FI STA CONNECTION EVENT");
//         }


//         // Store event data
//         wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*)event_data;
//         ESP_LOGI(TAG, "Station connected, BSSID = "MACSTR", SSID = %s (length: %d), Channel = %d, AuthMode = %d, AID = %d",
//                 MAC2STR(event->bssid), event->ssid, event->ssid_len, event->channel, event->authmode, event->aid);


//         // Create wifi device with event data
//         WifiDevice NewDevice{};
//         NewDevice.TimeOfConnection = esp_timer_get_time();
//         //NewDevice.IsRegisteredWithEspNow = false;
//         NewDevice.aid = event->aid;
//         memcpy(NewDevice.MacId, event->bssid, sizeof(NewDevice.MacId));


//         // Store device in class
//         ClassInstance->HostWifiDevice = NewDevice;


//         // Tell class it is connected to access point
//         ClassInstance->IsConnectedToAP = true;


//         // Trigger ESP-NOW task queue to add to ESP-NOW register
//         BaseType_t higherPriorityTaskWoken = pdFALSE;
        


//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             ESP_LOGW(TAG, "WI-FI STA CONNECTION EVENT COMPLETED");
//             printf("\n");
//         }
//     }

//     else if (event_id == WIFI_EVENT_STA_DISCONNECTED) 
//     {
//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             printf("\n");
//             ESP_LOGW(TAG, "WI-FI STA DISCONNECTION EVENT");
//         }


//         // Store event data
//         wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
//         ESP_LOGI(TAG, "Station "MACSTR" leave, SSID = %s, BSSID = "MACSTR", Reason = %d, RSSI = %d",
//                  MAC2STR(event->bssid), event->ssid, MAC2STR(event->bssid), event->reason, event->rssi);


//         if (ClassInstance->IsConnectedToAP)
//         {
//             // Copy wifi device data
//             WifiDevice TempDevice = ClassInstance->HostWifiDevice;

//             // Wipe wifi device from the class
//             memset(&ClassInstance->HostWifiDevice, 0, sizeof(ClassInstance->HostWifiDevice));

//             // Force all disconnected devices to attempt to deauth ESP-NOW
//             //TempDevice.IsRegisteredWithEspNow = true;

//             // Trigger ESP-NOW task queue to wipe from ESP-NOW register
//             BaseType_t higherPriorityTaskWoken = pdFALSE;
           
//         }


//         // Tell class it is NOT connected to access point
//         ClassInstance->IsConnectedToAP = false;


//         // Retry connection immediately
//         esp_wifi_disconnect();
//         esp_wifi_connect();


//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             ESP_LOGW(TAG, "WI-FI STA DISCONNECTION EVENT COMPLETED");
//             ESP_LOGI(TAG, "Disconnected from Wi-Fi, retrying...");
//             printf("\n");
//         }  
//     } 
// }

// // Software event handler for IP AP events
// void WifiClass::IpEventHandlerAp(void* arg, esp_event_base_t event_base,
//                                     int32_t event_id, void* event_data)
// {
//     if (event_id == IP_EVENT_AP_STAIPASSIGNED)
//     {
//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             printf("\n");
//             ESP_LOGW(TAG, "IP AP ASSIGNED EVENT");
//         }

//         ip_event_ap_staipassigned_t* event = (ip_event_ap_staipassigned_t*) event_data;
        
//         // Convert the assigned IP to a string
//         char ip_str[16];
//         esp_ip4addr_ntoa(&event->ip, ip_str, sizeof(ip_str));

//         ESP_LOGI(TAG, "Station assigned IP: %s", ip_str);

//         // Loop through the ClientWifiDeviceList and find the device by MAC address
//         for (auto& device : ClassInstance->ClientWifiDeviceList)
//         {
//             // Match by MAC address
//             if (memcmp(device.MacId, event->mac, sizeof(device.MacId)) == 0)
//             {
//                 // Found the device, store the IP as a string in the corresponding slot
//                 strncpy(device.IpAddress, ip_str, sizeof(device.IpAddress) - 1);
//                 device.IpAddress[sizeof(device.IpAddress) - 1] = '\0'; // Ensure null-termination

//                 ESP_LOGI(TAG, "Device " MACSTR " now has IP: %s", MAC2STR(event->mac), device.IpAddress);
//                 break;
//             }
//         }

//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             ESP_LOGW(TAG, "IP AP ASSIGNED EVENT COMPLETED");
//             printf("\n");
//         }
//     }
// }

// // Software event handler for IP STA events
// void WifiClass::IpEventHandlerSta(void* arg, esp_event_base_t event_base,
//                                     int32_t event_id, void* event_data)
// {
//     if (event_id == IP_EVENT_STA_GOT_IP) 
//     {
//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             printf("\n");
//             ESP_LOGW(TAG, "IP STA ASSIGNED EVENT");
//         }

//         char ip_str[16];

//         ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
//         esp_ip4addr_ntoa(&event->ip_info.ip, ip_str, sizeof(ip_str));

//         ESP_LOGI(TAG, "Got IP from network (STA Mode): %s", ip_str);

//         strncpy(ClassInstance->HostWifiDevice.IpAddress, ip_str,
//         sizeof(ClassInstance->HostWifiDevice.IpAddress) - 1);

//         // ESP_LOGI(TAG, "Stored IP bytes: %02X %02X %02X %02X  ...  %02X",
//         // (unsigned)ClassInstance->HostWifiDevice.IpAddress[0],
//         // (unsigned)ClassInstance->HostWifiDevice.IpAddress[1],
//         // (unsigned)ClassInstance->HostWifiDevice.IpAddress[2],
//         // (unsigned)ClassInstance->HostWifiDevice.IpAddress[3],
//         // (unsigned)ClassInstance->HostWifiDevice.IpAddress[15]);


//         ClassInstance->HostWifiDevice.IpAddress[
//             sizeof(ClassInstance->HostWifiDevice.IpAddress) - 1
//         ] = '\0';

//         ClassInstance->StaIpAcquired = true;

//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             ESP_LOGW(TAG, "IP STA ASSIGNED EVENT COMPLETED");
//             printf("\n");
//         }
//     }

//     else if (event_id == IP_EVENT_STA_LOST_IP)
//     {
//         if (ClassInstance->IsRuntimeLoggingEnabled)
//         {
//             printf("\n");
//             ESP_LOGW(TAG, "IP STA LOST EVENT");
//         }
//     }
// }



// void WifiClass::UdpRxTask(void* pvParameters)
// {
//     uint8_t rx[UDP_SLOT_SIZE];
//     sockaddr_in src{};
//     socklen_t slen = sizeof(src);

//     for (;;)
//     {
//         int n = recvfrom(ClassInstance->UdpSocket,
//                          rx,
//                          sizeof(rx),
//                          0,
//                          (struct sockaddr*)&src,
//                          &slen);

//         if (n > 0)
//         {
//             // Must start with 0x02, 0xB5
//             if (n >= 2 && rx[0] == 0x02 && rx[1] == 0xB5)
//             {
//                 const uint16_t copy_len = (n > (int)UDP_SLOT_SIZE) ? (uint16_t)UDP_SLOT_SIZE : (uint16_t)n;

//                 portENTER_CRITICAL(&ClassInstance->SlotMux);

//                 ClassInstance->BufferSlots[ClassInstance->SlotHead].len = copy_len;
//                 memcpy(ClassInstance->BufferSlots[ClassInstance->SlotHead].data, rx, copy_len);

//                 ClassInstance->SlotHead = (uint8_t)((ClassInstance->SlotHead + 1) % UDP_SLOTS);
//                 if (ClassInstance->SlotCount < UDP_SLOTS) ClassInstance->SlotCount++;

//                 portEXIT_CRITICAL(&ClassInstance->SlotMux);
//             }
//         }

//         vTaskDelay(1);
//     }
// }

// bool WifiClass::StartUdp(uint16_t Port, uint8_t Core)
// {
//     if (not ClassInstance->StaIpAcquired) return false;
//     if (ClassInstance->UdpStarted) return true;

//     ClassInstance->UdpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

//     if (ClassInstance->UdpSocket < 0) return false;

//     // Bind
//     sockaddr_in addr{};
//     addr.sin_family = AF_INET;
//     addr.sin_port = htons(Port);
//     addr.sin_addr.s_addr = htonl(INADDR_ANY);

//     if (bind(ClassInstance->UdpSocket, (struct sockaddr*)&addr, sizeof(addr)) != 0)
//     {
//         close(ClassInstance->UdpSocket);
//         ClassInstance->UdpSocket = -1;
//         return false;
//     }

//     // RX timeout so task can loop
//     timeval tv{};
//     tv.tv_sec = 0;
//     tv.tv_usec = 50000;
//     setsockopt(ClassInstance->UdpSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

//     // Create RX task (once)
//     BaseType_t ok = xTaskCreatePinnedToCore(
//         &WifiClass::UdpRxTask,
//         "udp_rx",
//         4096,
//         ClassInstance,
//         0,
//         &ClassInstance->UdpRxTaskHandle,
//         Core
//     );

//     if (ok != pdPASS)
//     {
//         close(ClassInstance->UdpSocket);
//         ClassInstance->UdpSocket = -1;
//         return false;
//     }

//     ClassInstance->UdpStarted = true;
//     return true;
// }

// size_t WifiClass::GetDataFromBuffer(uint8_t* DataOut, bool* DataAvailable)
// {
//     if (DataAvailable) *DataAvailable = false;
//     if (!DataOut) return 0;

//     portENTER_CRITICAL(&ClassInstance->SlotMux);

//     if (ClassInstance->SlotCount == 0)
//     {
//         portEXIT_CRITICAL(&ClassInstance->SlotMux);
//         return 0;
//     }

//     // Latest packet is the slot just before SlotHead
//     uint8_t latestIdx = (uint8_t)((ClassInstance->SlotHead + UDP_SLOTS - 1) % UDP_SLOTS);
//     uint16_t len = ClassInstance->BufferSlots[latestIdx].len;

//     // Sanity
//     if (len > UDP_SLOT_SIZE) len = UDP_SLOT_SIZE;

//     memcpy(DataOut, ClassInstance->BufferSlots[latestIdx].data, len);

//     // Consume it: move head backwards and reduce count
//     ClassInstance->SlotHead = latestIdx;
//     ClassInstance->SlotCount--;

//     // Optional: clear the consumed slot
//     ClassInstance->BufferSlots[latestIdx].len = 0;
//     memset(ClassInstance->BufferSlots[latestIdx].data, 0, UDP_SLOT_SIZE);

//     portEXIT_CRITICAL(&ClassInstance->SlotMux);

//     if (DataAvailable) *DataAvailable = (len > 0);
//     return (size_t)len;
// }



// // Function that sets up the system as an Access Point
// bool WifiClass::SetupWifiAP(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse)
// {
//     printf("\n");
//     ESP_LOGW(TAG, "SETUP WI-FI AP");
//     IsAp = true;


//     // Initialize NVS
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
//     {
//       ESP_ERROR_CHECK(nvs_flash_erase());
//       ret = nvs_flash_init();
//     }
//     if (ret != ESP_OK) 
//     {
//         ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
//         return false; 
//     }
//     ESP_LOGI(TAG, "1 - NVS Ready!");


//     // Initialize network interface
//     ret = esp_netif_init();
//     if (ret != ESP_OK) 
//     {
//         ESP_LOGE(TAG, "Failed to initialize network interface: %s", esp_err_to_name(ret));
//         return false;
//     }
//     ESP_LOGI(TAG, "2 - Network Interface Ready!");


//     // Create event loop
//     ret = esp_event_loop_create_default();
//     if (ret != ESP_OK) 
//     {
//         ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
//         return false;
//     }
//     ESP_LOGI(TAG, "3 - Event Loop Ready!");


//     // Set up the AP interface
//     esp_netif_t* netif = esp_netif_create_default_wifi_ap();
//     if (netif == nullptr) 
//     {
//         ESP_LOGE(TAG, "Failed to create default Wi-Fi AP interface");
//         return false;
//     }
//     ESP_LOGI(TAG, "4 - AP Interface Ready!");


//     // Initialize wifi stack with default configuration
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ret = esp_wifi_init(&cfg);
//     if (ret != ESP_OK) 
//     {
//         ESP_LOGE(TAG, "Failed to initialize Wi-Fi stack: %s", esp_err_to_name(ret));
//         return false;
//     }
//     ESP_LOGI(TAG, "5 - Wi-Fi Stack Ready!");


//     // Set the country region before initializing Wi-Fi
//     wifi_country_t country = 
//     {
//         .cc = "GB",        // UK country code
//         .schan = 1,        // Start channel
//         .nchan = 13,       // Number of channels available
//         .max_tx_power = 20 // Max TX power (can vary based on the region)
//     };
//     ret = esp_wifi_set_country(&country);
//     if (ret != ESP_OK) 
//     {
//         ESP_LOGE(TAG, "Failed to set country: %s", esp_err_to_name(ret));
//         return false;
//     }
//     ESP_LOGI(TAG, "6 - Country Set To GB!");


//     // Register event handler
//     ret = (esp_event_handler_instance_register(WIFI_EVENT,
//                                                 ESP_EVENT_ANY_ID,
//                                                 &WifiClass::WifiEventHandlerAp,
//                                                 ClassInstance,
//                                                 NULL));
//     if (ret != ESP_OK) 
//     {
//         ESP_LOGE(TAG, "Failed to register wifi event handler: %s", esp_err_to_name(ret));
//         return false;
//     }
//     ESP_LOGI(TAG, "7 - Wifi Event Handler Ready!");


//     // Register event handler
//     ret = esp_event_handler_instance_register(IP_EVENT,
//                                                 ESP_EVENT_ANY_ID,
//                                                 &WifiClass::IpEventHandlerAp,
//                                                 ClassInstance,
//                                                 NULL);
//     if (ret != ESP_OK)
//     {
//     ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
//     return false;       
//     }
//     ESP_LOGI(TAG, "8 - IP Event Handler Ready!");


//     // Configure AP settings
//     wifi_config_t wifi_config = 
//     {
//         .ap = 
//         {
//             .ssid = WIFI_SSID,
//             .password = WIFI_PASS,
//             .ssid_len = strlen(WIFI_SSID),
//             .channel = WIFI_CHANNEL,
//     #ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
//             .authmode = WIFI_AUTH_WPA3_PSK,
//             .max_connection = MAX_STA_CONN,
//             .pmf_cfg = 
//             {
//                 .required = true,
//             },
//             .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
//     #else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
//             .authmode = WIFI_AUTH_WPA2_PSK,
//             .max_connection = MAX_STA_CONN,
//             .pmf_cfg = 
//             {
//                 .required = true,
//             },
//     #endif
//         },
//     };
//     // Open access if no password specified
//     if (strlen(WIFI_PASS) == 0) 
//     {
//         wifi_config.ap.authmode = WIFI_AUTH_OPEN;
//     }
//     // Set the wifi mode to AP
//     ret = esp_wifi_set_mode(WIFI_MODE_AP);
//     if (ret != ESP_OK) 
//     {
//         ESP_LOGE(TAG, "Failed to set Wi-Fi mode: %s", esp_err_to_name(ret));
//         return false;
//     }
//     ESP_LOGI(TAG, "9 - Wi-Fi Mode Set To AP!");


//     // Apply previously made configuration
//     ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
//     if (ret != ESP_OK) 
//     {
//         ESP_LOGE(TAG, "Failed to apply Wi-Fi configuration: %s", esp_err_to_name(ret));
//         return false;
//     }
//     ESP_LOGI(TAG, "10 - Wi-Fi Configuration Applied!");


//     // Start wifi AP
//     ret = esp_wifi_start();
//     if (ret != ESP_OK) 
//     {
//         ESP_LOGE(TAG, "Failed to start Wi-Fi AP: %s", esp_err_to_name(ret));
//         return false;
//     }
//     ESP_LOGI(TAG, "11 - Wi-Fi AP Started!");


//     ret = esp_wifi_set_inactive_time(WIFI_IF_AP, Timeout);
//     if (ret != ESP_OK)
//     {
//         ESP_LOGE(TAG, "Failed to set Inactive Timer: %s", esp_err_to_name(ret));
//         return false;
//     }
//     ESP_LOGI(TAG, "12 - Inactive Timer Set To 10s!");


//     ESP_LOGI(TAG, "SSID: %s, Password: %s, Channel: %d",
//              WIFI_SSID, WIFI_PASS, WIFI_CHANNEL);
//     ESP_LOGW(TAG, "SETUP WI-FI AP SUCCESSFUL");    
//     printf("\n");
//     return true;
// }

// // Function that sets up the system as a Station
// bool WifiClass::SetupWifiSta(uint16_t UdpPort, uint16_t Timeout, uint8_t CoreToUse)
// {
//     printf("\n");
//     ESP_LOGW(TAG, "SETUP WI-FI STATION");
//     IsSta = true;
//     esp_err_t ret;


//     if (not ClassInstance->NvsReady)
//     {
//         //Initialize NVS
//         ret = nvs_flash_init();
//         if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
//         {
//             ESP_ERROR_CHECK(nvs_flash_erase());
//             ret = nvs_flash_init();
//         }
//         if (ret != ESP_OK) 
//         {
//             ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(ret));
//             return false; // Early return if the error occurs
//         }
//         ESP_LOGI(TAG, "1 - NVS Ready!");
//         ClassInstance->NvsReady = true;
//     }



//     if (not ClassInstance->NetifReady)
//     {
//         // Initialize network interface
//         ret = esp_netif_init();
//         if (ret != ESP_OK) 
//         {
//             ESP_LOGE(TAG, "Failed to initialize network interface: %s", esp_err_to_name(ret));
//             return false;
//         }
//         ESP_LOGI(TAG, "2 - Network Interface Ready!");
//         ClassInstance->NetifReady = true;  
//     }



//     if (not ClassInstance->EventLoopReady)
//     {
//         // Create event loop
//         ret = esp_event_loop_create_default();
//         if (ret != ESP_OK) 
//         {
//             ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
//             return false;
//         }
//         ESP_LOGI(TAG, "3 - Event Loop Ready!");
//         ClassInstance->EventLoopReady = true;
//     }



//     if (not ClassInstance->StationInterfaceReady)
//     {
//         // Set up the Station interface
//         ClassInstance->StaNetif = esp_netif_create_default_wifi_sta();
//         if (ClassInstance->StaNetif == nullptr) 
//         {
//             ESP_LOGE(TAG, "Failed to create default Wi-Fi Station interface");
//             return false;
//         }
//         ESP_LOGI(TAG, "4 - Station Interface Ready!");
//         ClassInstance->StationInterfaceReady = true;
//     }



//     if (not ClassInstance->WifiStackReady)
//     {
//         // Initialize Wi-Fi stack with default configuration
//         wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//         ret = esp_wifi_init(&cfg);
//         if (ret != ESP_OK) 
//         {
//             ESP_LOGE(TAG, "Failed to initialize Wi-Fi stack: %s", esp_err_to_name(ret));
//             return false;
//         }
//         ESP_LOGI(TAG, "5 - Wi-Fi Stack Ready!");
//         ClassInstance->WifiStackReady = true;
//     }



//     if (not ClassInstance->CountrySet)
//     {
//         // Set the country region before initializing Wi-Fi
//         wifi_country_t country = 
//         {
//             .cc = "GB",        // UK country code
//             .schan = 1,        // Start channel
//             .nchan = 13,       // Number of channels available
//             .max_tx_power = 20 // Max TX power (can vary based on the region)
//         };
//         ret = esp_wifi_set_country(&country);
//         if (ret != ESP_OK) {
//             ESP_LOGE(TAG, "Failed to set country: %s", esp_err_to_name(ret));
//             return false;
//         }
//         ESP_LOGI(TAG, "6 - Country Set To GB!");
//         ClassInstance->CountrySet = true;
//     }



//     if (not ClassInstance->StaEventHandlersRegistered)
//     {
//         // Register event handler
//         ret = esp_event_handler_instance_register(WIFI_EVENT,
//                                                 ESP_EVENT_ANY_ID,
//                                                 &WifiClass::WifiEventHandlerSta,
//                                                 ClassInstance,
//                                                 NULL);
//         if (ret != ESP_OK) 
//         {
//             ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
//             return false;
//         }
//         ESP_LOGI(TAG, "7 - Wifi Event Handler Ready!");


//         // Register event handler
//         ret = esp_event_handler_instance_register(IP_EVENT,
//                                                 ESP_EVENT_ANY_ID,
//                                                 &WifiClass::IpEventHandlerSta,
//                                                 ClassInstance,
//                                                 NULL);
//         if (ret != ESP_OK)
//         {
//             ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(ret));
//             return false;       
//         }
//         ESP_LOGI(TAG, "8 - IP Event Handler Ready!");
//         ClassInstance->StaEventHandlersRegistered = true;
//     }



//     // Configure Station settings
//     wifi_config_t wifi_config = {};
//     strcpy(reinterpret_cast<char*>(wifi_config.sta.ssid), WIFI_SSID);
//     strcpy(reinterpret_cast<char*>(wifi_config.sta.password), WIFI_PASS);
//     wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;



//     if (not ClassInstance->SetWifiModeDone)
//     {
//         // Set the Wi-Fi mode to Station
//         ret = esp_wifi_set_mode(WIFI_MODE_STA);
//         if (ret != ESP_OK) 
//         {
//             ESP_LOGE(TAG, "Failed to set Wi-Fi mode: %s", esp_err_to_name(ret));
//             return false;
//         }
//         ESP_LOGI(TAG, "9 - Wi-Fi Mode Set To Station!");
//         ClassInstance->SetWifiModeDone = true;
//     }



//     if (not ClassInstance->ApplyConfigDone)
//     {
//         // Apply Wi-Fi configuration
//         ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
//         if (ret != ESP_OK) 
//         {
//             ESP_LOGE(TAG, "Failed to apply Wi-Fi configuration: %s", esp_err_to_name(ret));
//             return false;
//         }
//         ESP_LOGI(TAG, "10 - Wi-Fi Configuration Applied!");
//         ClassInstance->ApplyConfigDone = true;
//     }



//     if (not ClassInstance->StartWifiDone)
//     {
    
//         // Start Wi-Fi in Station mode
//         ret = esp_wifi_start();
//         if (ret != ESP_OK) 
//         {
//             ESP_LOGE(TAG, "Failed to start Wi-Fi Station: %s", esp_err_to_name(ret));
//             return false;
//         }
//         ESP_LOGI(TAG, "11 - Wi-Fi Station Started!");


//         ret = esp_wifi_set_inactive_time(WIFI_IF_STA, Timeout);
//         if (ret != ESP_OK)
//         {
//             ESP_LOGE(TAG, "Failed to set Inactive Timer: %s", esp_err_to_name(ret));
//             return false;
//         }
//         ESP_LOGI(TAG, "12 - Inactive Timer Set To 10s!");
//         ClassInstance->StartWifiDone = true;
//     }



//     if (not ClassInstance->UdpStarted)
//     {
//         StartUdp(UdpPort, CoreToUse);
//     }

//     // if (not ClassInstance->UdpSocketCreated)
//     // {
//     //     if (!SetupUdpSocket(UdpPort))
//     //     {
//     //         ESP_LOGE(TAG, "Failed to bind UDP socket");
//     //         return false;
//     //     }
//     //     ESP_LOGI(TAG, "13 - UDP Socket Bound!");
//     //     ClassInstance->UdpSocketCreated = true;
//     // }



//     // if (not ClassInstance->UdpTasksCreated)
//     // {
//     //     UdpPollingTaskHandle = xTaskCreateStaticPinnedToCore
//     //     (
//     //         UdpPollingTask,                     // Task function
//     //         "Udp Polling Task",                 // Task name
//     //         UdpPollingTaskStackSize,            // Stack depth
//     //         NULL,                               // Parameters to pass
//     //         UdpPollingTaskPriority,             // Low priority
//     //         UdpPollingTaskStack,                // Preallocated stack memory
//     //         &UdpPollingTaskTCB,                 // Preallocated TCB memory
//     //         CoreToUse                           // Core assigned
//     //     );   
//     //     if (UdpPollingTaskHandle == NULL)
//     //     {
//     //         ESP_LOGE(TAG, "Failed to create UDP polling task");
//     //         return false;
//     //     }
//     //     ESP_LOGI(TAG, "14 - UDP Polling Task Created!");
//     //
//     //
//     //     UdpProcessingQueue = xQueueCreate(10, sizeof(UdpPacket*)); 
//     //     if (UdpProcessingQueue == NULL) 
//     //     {
//     //         ESP_LOGE(TAG, "Failed to create UDP queue");
//     //         return false;
//     //     }
//     //     ESP_LOGI(TAG, "15 - UdpProcessingQueue Ready!");
//     //
//     //
//     //     UdpProcessingTaskHandle = xTaskCreateStaticPinnedToCore
//     //     (
//     //         UdpProcessingTask,                      // Task function
//     //         "Udp Processing Task",                  // Task name
//     //         UdpProcessingTaskStackSize,             // Stack depth
//     //         NULL,                                   // Parameters to pass
//     //         UdpProcessingTaskPriority,              // High priority
//     //         UdpProcessingTaskStack,                 // Preallocated stack memory
//     //         &UdpProcessingTaskTCB,                  // Preallocated TCB memory
//     //         CoreToUse                               // Core assigned
//     //     );   
//     //     if (UdpProcessingTaskHandle == NULL)
//     //     {
//     //         ESP_LOGE(TAG, "Failed to create UDP processing task");
//     //         return false;
//     //     }
//     //     ESP_LOGI(TAG, "16 - UDP Processing Task Created!");
//     //
//     //
//     //     UdpSystemTaskHandle = xTaskCreateStaticPinnedToCore
//     //     (
//     //         UdpSystemTask,                          // Task function
//     //         "Udp System Task",                      // Task name
//     //         UdpSystemTaskStackSize,                 // Stack depth
//     //         NULL,                                   // Parameters to pass
//     //         UdpSystemTaskPriority,                  // High priority
//     //         UdpSystemTaskStack,                     // Preallocated stack memory
//     //         &UdpSystemTaskTCB,                      // Preallocated TCB memory
//     //         CoreToUse                               // Core assigned
//     //     );   
//     //     if (UdpProcessingTaskHandle == NULL)
//     //     {
//     //         ESP_LOGE(TAG, "Failed to create UDP System task");
//     //         return false;
//     //     }
//     //     ESP_LOGI(TAG, "17 - UDP System Task Created!");
//     //     ClassInstance->UdpTasksCreated = true;
//     // }



//     ESP_LOGW(TAG, "SETUP WI-FI STATION SUCCESSFUL");     
    

    
//     if (not ClassInstance->IsConnectedToAP)
//     {
//         ESP_LOGI(TAG, "Connecting to SSID: %s", WIFI_SSID);
//         esp_wifi_connect();
//     }

//     printf("\n");
//     return true;
// }





// //==============================================================================// 
// //                                                                              //
// //                       Public Setup Functions                                 //
// //                                                                              //
// //==============================================================================// 

// // Function that sets up the wifi system based on the menu configuration
// bool WifiClass::SetupWifi(uint8_t CoreToUse)
// {
//     printf("\n");
//     ESP_LOGW(TAG, "SetupWifi Executed!");


//     // AP
//     if (WIFI_MODE == 0)
//     {
//         if (!SetupWifiAP(UDP_PORT, 10, CoreToUse))
//         {
//             ESP_LOGI(TAG, "SetupWifiAP Failed!");
//             return false;
//         }
//     }


//     // Station
//     if (WIFI_MODE == 1)
//     {
//         if (!SetupWifiSta(UDP_PORT, 10, CoreToUse))
//         {
//             ESP_LOGI(TAG, "SetupWifiSta Failed!");
//             return false;
//         }
//     }


//     ESP_LOGW(TAG, "SetupWifi Successful!");
//     printf("\n");
//     return true;
// }





// //==============================================================================// 
// //                                                                              //
// //                             Commands                                         //
// //                                                                              //
// //==============================================================================//

// // Function that sends a message as a UDP packet to a destination
// bool WifiClass::SendUdpPacket(const char* Data, const char* DestinationIP, uint16_t DestinationPort)
// {
//     return false;
//     // Needs remaking

//     // if ((IsAp and (GetNumClientsConnected() == 0)) or
//     //      (IsSta and !GetIsConnectedToHost()))
//     // {
//     //     if (IsRuntimeLoggingEnabled)
//     //     {
//     //         ESP_LOGE(TAG, "Not Connected");
//     //     }
//     //     return false;
//     // }

//     // struct sockaddr_in DestinationAddress;

//     // memset(&DestinationAddress, 0, sizeof(DestinationAddress));

//     // DestinationAddress.sin_family = AF_INET;

//     // if (inet_pton(AF_INET, DestinationIP, &DestinationAddress.sin_addr) <= 0)
//     // {
//     //     if (IsRuntimeLoggingEnabled)
//     //     {
//     //         ESP_LOGE(TAG, "Invalid IP address: %s", DestinationIP);
//     //     }
//     //     return false;
//     // }

//     // DestinationAddress.sin_port = htons(DestinationPort);

//     // int SentBytes = sendto(UdpSocketFD, Data, strlen(Data), 0, (struct sockaddr*)&DestinationAddress, sizeof(DestinationAddress));

//     // if (SentBytes < 0)
//     // {
//     //     if (IsRuntimeLoggingEnabled)
//     //     {
//     //         ESP_LOGE(TAG, "Failed to send UDP packet, error = %d", errno);
//     //     }
//     //     return false;
//     // }
//     // return true;
// }





// //==============================================================================// 
// //                                                                              //
// //                             Get / Set                                        //
// //                                                                              //
// //==============================================================================//

// size_t WifiClass::GetNumClientsConnected()
// {
//     return ClientWifiDeviceList.size();
// }

// bool WifiClass::GetIsConnectedToHost()
// {
//     return IsConnectedToAP;
// }

// bool WifiClass::GetIsAp()
// {
//     return IsAp;
// }

// bool WifiClass::GetIsSta()
// {
//     return IsSta;
// }

// const char* WifiClass::GetApIpAddress()
// {
//     return HostWifiDevice.IpAddress;
// }

// void WifiClass::SetRuntimeLogging(bool EnableRuntimeLogging)
// {
//     IsRuntimeLoggingEnabled = EnableRuntimeLogging;
// }


















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
{}

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

                StaClassInstance->DataInBuffer = (StaClassInstance->LastPositionWritten > 0);
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

    if (DataInBuffer && Available > 0)
    {
        memcpy(DataToReceive, RxData, Available);

        Copied = Available;

        LastPositionWritten = 0;

        DataInBuffer = false;

        *IsDataAvailable = true;
    }
    else
    {
        DataInBuffer = false;

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

    if (false) // Placeholder for access point and ApSta pointers
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
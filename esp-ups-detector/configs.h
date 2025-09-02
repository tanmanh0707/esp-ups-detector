#pragma once

#define CONFIG_UDP_SERVER_PORT                7792
#define CONFIG_UDP_CLIENT_PORT                7792
#define CONFIG_TCP_SERVER_PORT                7792

#define CONFIG_WIFI_AP_SSID                   "UPS Power Detector AP"
#define CONFIG_WIFI_AP_PASSWORD               "12345678"
#define CONFIG_WIFI_CONNECT_TIMEOUT           20000

#define CONFIG_BUILTIN_LED_PIN                8

#define CONFIG_POWER_OFF_CURRENT_VOL          50.0  //Voltage
#define CONFIG_POWER_CHANGE_TIME              10000
#define CONFIG_POWER_CHANGE_SYNC_TIME         CONFIG_POWER_CHANGE_TIME
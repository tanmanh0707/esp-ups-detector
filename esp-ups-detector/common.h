#pragma once

#include <Arduino.h>
#include <AsyncUDP.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include "configs.h"
#include "ap_webpages.h"

#define MEMCMP_EQUAL                          0

typedef uint16_t                              DeviceId_t;

typedef enum {
  LED_CMD_OFF = (0),
  LED_CMD_STARTUP,
  LED_CMD_WIFI_CONNECTING,
  LED_CMD_WIFI_CONNECTED,
  LED_CMD_WIFI_FAILED,
  LED_CMD_AP_MODE,
  LED_CMD_POWER_OFF,
  LED_CMD_MAX
} LedCtrlCmd_e;

typedef struct {
  uint8_t cmd;
  uint8_t *data;
  uint16_t len;
} QueueMsg_st;


void MAIN_StartAP();

/* WIFI MESH */
void WIFI_Init();
void WIFI_AccessPoint();
void WIFI_AP_ServerLoop();

/* UDP */
void SERVER_Init();
void SERVER_Send(String &msg);

/* DATABASE */
void DB_GetWifiCredentials(String &ssid, String &password);
void DB_SetWifiCredentials(String &ssid, String &password);

/* LED */
void LED_Init();
void LED_SendCmd(LedCtrlCmd_e cmd);

/* SENSOR */
void SENSOR_Init();
void SENSOR_Loop();
void SENSOR_HandleTcpMsg(uint8_t *data, size_t len);

#include "common.h"

#define TCP_QUEUE_SIZE                        10

static AsyncUDP _udpServer;
static AsyncServer _tcpServer(CONFIG_TCP_SERVER_PORT);
const char *udp_broadcast_msg = "Where are you?";
const char *udp_response_msg = "Here I am";
static AsyncClient *_tcpClient = nullptr;
static WebServer _apServer(80);
static TaskHandle_t _tcpTaskHdl = NULL;
static QueueHandle_t _tcpQ = NULL;

static void tcp_handler_task(void *param);

void LocalTcpSend(uint8_t cmd, uint8_t *data, uint16_t len, bool copy = true)
{
  if (_tcpQ)
  {
    QueueMsg_st msg = { cmd, data, len };
    if (copy && len) {
      msg.data = (uint8_t *)malloc(len);
      if (msg.data) {
        memcpy(msg.data, data, len);
      }
    }

    if (xQueueSend(_tcpQ, &msg, 0) != pdTRUE) {
      log_e("Send queue failed!");
      if (copy && len && msg.data) {
        free(msg.data);
        msg.data = NULL;
      }
    }
  }
}

void SERVER_Init()
{
  /* UDP Server */
  _udpServer.onPacket([](AsyncUDPPacket packet) {
    String packet_str = String((char *)packet.data(), packet.length());
    log_i("New packet: %s", packet_str.c_str());
    if (packet.length() == strlen(udp_broadcast_msg)) {
      if (strncmp((const char *)packet.data(), udp_broadcast_msg, packet.length()) == 0) {
        _udpServer.writeTo((const uint8_t *)udp_response_msg, strlen(udp_response_msg), packet.localIP(), CONFIG_UDP_CLIENT_PORT);
      }
    }
  });

  _udpServer.listen(CONFIG_UDP_SERVER_PORT);
  log_i("UDP Server listening on port %d", CONFIG_UDP_SERVER_PORT);

  /* TCP Server */
  _tcpServer.onClient([] (void *arg, AsyncClient *client) {
    log_i("New client connected! IP: "MACSTR" ", MAC2STR(client->remoteIP()));
    _tcpClient = client;

    client->onDisconnect([](void *arg, AsyncClient *client) {
      log_i("** client has been disconnected: %" PRIu16 "", client->localPort());
      _tcpClient = nullptr;
      client->close(true);
      delete client;
    });

    client->onData([](void *arg, AsyncClient *client, void *data, size_t len) {
      log_d("** data received by client: %" PRIu16 ": len=%u", client->localPort(), len);
      log_d("%*s", len, data);
      SENSOR_HandleTcpMsg((uint8_t *)data, len);
    });
  }, NULL);

  if (_tcpQ == NULL) {
    _tcpQ = xQueueCreate(TCP_QUEUE_SIZE, sizeof(QueueMsg_st));
  }

  if (_tcpTaskHdl == NULL) {
    xTaskCreate(tcp_handler_task, "tcp_handler_task", 8192, NULL, 1, &_tcpTaskHdl);
  }

  _tcpServer.begin();
}

void SERVER_Send(String &msg)
{
  if (_tcpClient) {
    if ( ! _tcpClient->write(msg.c_str())) {
      log_e("TCP Write failed!");
    } else {
      log_i("Sent: %s", msg.c_str());
    }
  }
}

void LocalSendTcpResponse(bool ret)
{
  String response = String("{\"message\":\"") + (ret? String("success") : String("failed")) + String("\"}");
  SERVER_Send(response);
}

void tcp_handler_task(void *param)
{
  QueueMsg_st msg;

  while (1)
  {
    if (xQueueReceive(_tcpQ, &msg, portMAX_DELAY) == pdTRUE)
    {
      if (msg.data && msg.len)
      {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, msg.data, msg.len);

        if (error == DeserializationError::Ok) {
          
        }
      }

      /* Free resources */
      if (msg.data)
      {
        free(msg.data);
        msg.data = NULL;
      }
      msg.len = 0;
    }
  }
}

void wifi_ap_task(void *param)
{
  while (1) {
    WIFI_AP_ServerLoop();
    delay(1);
  }
}

bool WIFI_ValidateWifiCredentials(String &ssid, String &pass)
{
  return ! (ssid.length() < 4 || ((0 < pass.length() && pass.length() < 8)));
}

void WIFI_AccessPoint()
{
  LED_SendCmd(LED_CMD_AP_MODE);

  WiFi.softAP(CONFIG_WIFI_AP_SSID, CONFIG_WIFI_AP_PASSWORD, 6);
  log_i("Access Point IP: %s", WiFi.softAPIP().toString().c_str());

  _apServer.on("/", []() {
    _apServer.send(200, "text/html", index_html);
  });

  _apServer.on("/settings", HTTP_POST, []() {
    String ssid = _apServer.arg("ssid");
    String pass = _apServer.arg("password");
    log_i("Username: %s - Password: %s", ssid.c_str(), pass.c_str());

    DB_SetWifiCredentials(ssid, pass);
    _apServer.send(200, "text/plain", "Successful");
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP.restart();
  });

  _apServer.begin();

  if (xTaskCreate(wifi_ap_task, "wifi_ap_task", 8*1024, NULL, 1, NULL) == pdFALSE) {
    log_e("WiFi AP Create Task Failed!");
    delay(5000);
    ESP.restart();
  }
}

void WIFI_Init()
{
  String ssid, pass;
  DB_GetWifiCredentials(ssid, pass);

  if (WIFI_ValidateWifiCredentials(ssid, pass))
  {
    log_i("Connecting to WiFi: %s - %s", ssid.c_str(), pass.c_str());
    LED_SendCmd(LED_CMD_WIFI_CONNECTING);

    unsigned long connect_time = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      if (millis() - connect_time >= CONFIG_WIFI_CONNECT_TIMEOUT) {
        log_e("WiFi Connect Failed! Goto Access Point mode!");
        WIFI_AccessPoint();
      }
    }

    if (WiFi.status() == WL_CONNECTED) {
      LED_SendCmd(LED_CMD_OFF);
      SERVER_Init();
    }
  }
  else
  {
    log_e("Invalid WiFi Credentials! Goto Acccess Point mode!");
    WIFI_AccessPoint();
  }
}

void WIFI_AP_ServerLoop() {
  if (WiFi.getMode() == WIFI_AP) {
    _apServer.handleClient();
  }
}

#include "common.h"

#define RX_PZEM               4
#define TX_PZEM               3

#define SCALE_V               (0.1)
#define SCALE_A               (0.001)
#define SCALE_P               (0.1)
#define SCALE_E               (1)
#define SCALE_H               (0.1)
#define SCALE_PF              (0.01)

#define PZEM_CONVERT(low,high,scale)        (((high<<8) + low) * scale)
#define PZEM_GET_VALUE(unit, scale)         (float)(PZEM_CONVERT(myBuf[_##unit##_L__], myBuf[_##unit##_H__],scale))

#define PZEM_SERIAL           Serial1

enum{
  _address__ = 0,
  _byteSuccess__,
  _numberOfByte__,
  _voltage_H__,
  _voltage_L__,
  _ampe_H__,
  _ampe_L__,
  _ampe_1H__,
  _ampe_1L__,
  _power_H__,
  _power_L__,
  _power_1H__,
  _power_1L__,
  _energy_H__,
  _energy_L__,
  _energy_1H__,
  _energy_1L__,
  _freq_H__,
  _freq_L__,
  _powerFactor_H__,
  _powerFactor_L__,
  _nouse4H__,
  _nouse5L__,
  _crc_H__,
  _crc_L__,
  RESPONSE_SIZE
};

typedef enum {
  POWER_STARTUP = (0),
  POWER_ON,
  POWER_OFF,
  POWER_OFF_MONITOR,
  POWER_OFF_SYNCING,
  POWER_ON_MONITOR,
  POWER_ON_SYNCING,
} PowerStates_e;

static const byte getValue_para[8] = {0xf8, 0x04, 0x00, 0x00, 0x00, 0x0a, 0x64, 0x64};
static PowerStates_e state_ = POWER_STARTUP;
static float currentVol_ = 0.0;
static bool powerOn_ = true;
static bool isSynced_ = false;

static void sensor_handling_task(void *param);

#define STATE_CHANGE(new_state) {       \
  log_i("State change: %s", LocalGetStateStr(new_state)); \
  state_ = new_state;                   \
}

const char *LocalGetStateStr(PowerStates_e state)
{
  const char *str = NULL;
  switch (state)
  {
    case POWER_STARTUP: str = "POWER_STARTUP"; break;
    case POWER_ON: str = "POWER_ON"; break;
    case POWER_OFF: str = "POWER_OFF"; break;
    case POWER_OFF_MONITOR: str = "POWER_OFF_MONITOR"; break;
    case POWER_OFF_SYNCING: str = "POWER_OFF_SYNCING"; break;
    case POWER_ON_MONITOR: str = "POWER_ON_MONITOR"; break;
    case POWER_ON_SYNCING: str = "POWER_ON_SYNCING"; break;
    default: str = "Unknown"; break;
  }
  return str;
}

void SENSOR_Init() {
  int TX_ESP = RX_PZEM;
  int RX_ESP = TX_PZEM;
  PZEM_SERIAL.begin(9600, SERIAL_8N1, RX_ESP, TX_ESP);

  if (xTaskCreate(sensor_handling_task, "sensor_handling_task", 8*1024, NULL, 1, NULL) == pdFALSE) {
    log_e("Sensor Handling Create Task Failed!");
  }
}

void SENSOR_Loop() {
  static unsigned long delay_time = 0;
  static unsigned long powerOffTime = 0;

  if (millis() - delay_time > 1000) {
    delay_time = millis();
  } else {
    return;
  }

  while (PZEM_SERIAL.available()) {
    PZEM_SERIAL.read();
  }

  PZEM_SERIAL.write(getValue_para, sizeof(getValue_para));

  unsigned long read_time = millis();
  bool b_complete = false;
  uint8_t myBuf[RESPONSE_SIZE] = {0};

  while ((millis() - read_time) < 100) {
    if (PZEM_SERIAL.available()) {
      PZEM_SERIAL.readBytes(myBuf, RESPONSE_SIZE);
      b_complete = true;
      yield();
      break;
    }
  }

  if (b_complete) {
    currentVol_ = PZEM_GET_VALUE(voltage,SCALE_V);
    // Serial.printf("Voltage: %.2f\n", currentVol_);
  } else {
    log_d("PZEM Read failed!");
    currentVol_ = 0.0;
  }
}

void SENSOR_HandleTcpMsg(uint8_t *data, size_t len)
{
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data, len);

  log_i("%*s", len, data);
  if (error == DeserializationError::Ok) {
    String status = doc["status"].as<String>();
    if (status == "on") {
      if (state_ == POWER_ON_SYNCING) {
        isSynced_ = true;
      }
    } else if (status == "off") {
      if (state_ == POWER_OFF_SYNCING) {
        isSynced_ = true;
      }
    }
  }
}

void sensor_handling_task(void *param)
{
  unsigned long off_time = 0, on_time = 0, sync_time = 0;

  while (1)
  {
    SENSOR_Loop();
    bool isPowerOff = currentVol_ < CONFIG_POWER_OFF_CURRENT_VOL;

    switch (state_)
    {
      case POWER_STARTUP:
        if (isPowerOff) {
          STATE_CHANGE(POWER_OFF_MONITOR);
          off_time = millis();
        } else {
          STATE_CHANGE(POWER_ON_MONITOR);
          on_time = millis();
        }
        break;

      case POWER_ON:
        if (isPowerOff) {
          STATE_CHANGE(POWER_OFF_MONITOR);
          off_time = millis();
        }
        break;

      case POWER_OFF_MONITOR:
        if ( ! isPowerOff) {
          STATE_CHANGE(POWER_ON_MONITOR);
        } else {
          if (millis() - off_time >= CONFIG_POWER_CHANGE_TIME) {
            STATE_CHANGE(POWER_OFF_SYNCING);
            isSynced_ = false;
            sync_time = 0;
          }
        }
        break;

      case POWER_OFF_SYNCING:
        if (sync_time == 0 || millis() - sync_time > CONFIG_POWER_CHANGE_SYNC_TIME) {
          String msg = "{\"status\":\"off\"}";
          SERVER_Send(msg);
          sync_time = millis();
        }

        if ( ! isPowerOff) {
          STATE_CHANGE(POWER_ON_MONITOR);
          on_time = millis();
        }

        if (isSynced_) {
          STATE_CHANGE(POWER_OFF);
          LED_SendCmd(LED_CMD_POWER_OFF);
        }
        break;

      case POWER_OFF:
        if ( ! isPowerOff) {
          STATE_CHANGE(POWER_ON_MONITOR);
          on_time = millis();
        }
        break;

      case POWER_ON_MONITOR:
        if (isPowerOff) {
          STATE_CHANGE(POWER_OFF_MONITOR);
        } else if (millis() - on_time >= CONFIG_POWER_CHANGE_TIME) {
          STATE_CHANGE(POWER_ON_SYNCING);
          isSynced_ = false;
          sync_time = 0;
        }
        break;

      case POWER_ON_SYNCING:
        if (sync_time == 0 || millis() - sync_time > CONFIG_POWER_CHANGE_SYNC_TIME) {
          String msg = "{\"status\":\"on\"}";
          SERVER_Send(msg);
          sync_time = millis();
        }

        if (isPowerOff) {
          STATE_CHANGE(POWER_OFF_MONITOR);
          off_time = millis();
        }

        if (isSynced_) {
          STATE_CHANGE(POWER_ON);
          LED_SendCmd(LED_CMD_OFF);
        }
        break;
    }
    delay(1000);
  }

}

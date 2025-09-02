#include "common.h"

void setup()
{
  Serial.begin(115200);
  delay(2000);

  SENSOR_Init();
  LED_Init();
  WIFI_Init();
}

void loop()
{
}
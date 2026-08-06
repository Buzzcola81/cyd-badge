#include <Arduino.h>

#include "app_logic.h"

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println(kProjectName);
}

void loop()
{
    delay(1000);
}

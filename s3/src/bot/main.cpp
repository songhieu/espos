// EspOS Bot firmware (env bot128): src/bot/app.cpp has the main loop.
#include <Arduino.h>

#include "app.h"

void setup() { bot::app::begin(); }
void loop() { bot::app::loop(); }

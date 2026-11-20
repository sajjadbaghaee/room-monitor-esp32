#pragma once

#include <Arduino.h>

namespace TimeManager
{
void begin();
void update();
bool systemTimeValid();
const String &dateText();
const String &timeText();
} // namespace TimeManager

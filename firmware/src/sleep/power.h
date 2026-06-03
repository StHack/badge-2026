#pragma once
#include <stdint.h>

enum class WakeCause : uint8_t {
    Cold,   // first power-on or deep-sleep reset
    Timer,  // RTC periodic timer
    GPIO,   // button press
};

// Read the cause of the most recent wakeup.
WakeCause powerGetWakeCause();

// Configure RTC timer + GPIO wakeup sources, then enter ESP32 light sleep.
// Returns when the device wakes. Call powerGetWakeCause() after.
void powerEnterLightSleep(uint32_t timerSec);

#include "power.h"
#include "../hal/buttons.h"
#include <Arduino.h>
#include <esp_sleep.h>

WakeCause powerGetWakeCause() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    switch (cause) {
        case ESP_SLEEP_WAKEUP_TIMER: return WakeCause::Timer;
        case ESP_SLEEP_WAKEUP_GPIO:  return WakeCause::GPIO;
        default:                     return WakeCause::Cold;
    }
}

void powerEnterLightSleep(uint32_t timerSec) {
    // RTC timer wake source
    esp_sleep_enable_timer_wakeup((uint64_t)timerSec * 1000000ULL);

    // GPIO wake source (any button pressed = LOW)
    buttonsEnableWake();

    esp_light_sleep_start();
    // Execution resumes here after wakeup
}

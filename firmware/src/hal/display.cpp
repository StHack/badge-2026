#include "display.h"
#include "../config/hardware.h"
#include <Arduino.h>

TwoWire          gLeftBus(0);
TwoWire          gRightBus(1);
Adafruit_SSD1306 gLeftDisplay(kDisplayWidth, kDisplayHeight, &gLeftBus, -1);
Adafruit_SSD1306 gRightDisplay(kDisplayWidth, kDisplayHeight, &gRightBus, -1);

bool displayInit(Adafruit_SSD1306& d, TwoWire& bus, uint8_t sda, uint8_t scl,
                 const char* label) {
    bus.begin(sda, scl);
    if (!d.begin(SSD1306_SWITCHCAPVCC, kDisplayAddr)) {
        Serial.printf("[display] %s init FAILED\n", label);
        return false;
    }
    d.clearDisplay();
    d.display();
    Serial.printf("[display] %s ready\n", label);
    return true;
}

void displayOn(Adafruit_SSD1306& d) {
    d.ssd1306_command(SSD1306_DISPLAYON);
}

void displayOff(Adafruit_SSD1306& d) {
    d.ssd1306_command(SSD1306_DISPLAYOFF);
}

void displaysClear() {
    gLeftDisplay.clearDisplay();
    gLeftDisplay.display();
    gRightDisplay.clearDisplay();
    gRightDisplay.display();
}

void displaysOn() {
    displayOn(gLeftDisplay);
    displayOn(gRightDisplay);
}

void displaysOff() {
    displayOff(gLeftDisplay);
    displayOff(gRightDisplay);
}

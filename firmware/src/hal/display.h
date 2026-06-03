#pragma once
#include <Adafruit_SSD1306.h>
#include <Wire.h>

// Hardware display handles — only the HAL and rendering code use these directly.
extern TwoWire          gLeftBus;
extern TwoWire          gRightBus;
extern Adafruit_SSD1306 gLeftDisplay;
extern Adafruit_SSD1306 gRightDisplay;

// Initialise one display on a given I2C bus. Returns true on success.
bool displayInit(Adafruit_SSD1306& d, TwoWire& bus, uint8_t sda, uint8_t scl,
                 const char* label);

void displayOn(Adafruit_SSD1306& d);
void displayOff(Adafruit_SSD1306& d);

// Clear both displays (black screen).
void displaysClear();
// Turn both display panels on/off (SSD1306_DISPLAYON/OFF).
void displaysOn();
void displaysOff();

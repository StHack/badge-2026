#include "eye_calibration.h"
#include "../config/hardware.h"
#include <Preferences.h>
#include <Arduino.h>

// Factory defaults — adjust to match physical cutout geometry
EyeWindow gEyeCal = {14, 0, 121, 52, 46, 57};

void eyeCalLoad() {
    Preferences p;
    if (!p.begin(kNvsEyeCal, true)) return;
    gEyeCal.tlX = p.getShort("tlX", gEyeCal.tlX);
    gEyeCal.tlY = p.getShort("tlY", gEyeCal.tlY);
    gEyeCal.brX = p.getShort("brX", gEyeCal.brX);
    gEyeCal.brY = p.getShort("brY", gEyeCal.brY);
    gEyeCal.bmX = p.getShort("bmX", gEyeCal.bmX);
    gEyeCal.bmY = p.getShort("bmY", gEyeCal.bmY);
    p.end();
    Serial.printf("[eyecal] tl=(%d,%d) br=(%d,%d) bm=(%d,%d)\n",
                  gEyeCal.tlX, gEyeCal.tlY, gEyeCal.brX, gEyeCal.brY,
                  gEyeCal.bmX, gEyeCal.bmY);
}

void eyeCalSave() {
    Preferences p;
    p.begin(kNvsEyeCal, false);
    p.putShort("tlX", gEyeCal.tlX);
    p.putShort("tlY", gEyeCal.tlY);
    p.putShort("brX", gEyeCal.brX);
    p.putShort("brY", gEyeCal.brY);
    p.putShort("bmX", gEyeCal.bmX);
    p.putShort("bmY", gEyeCal.bmY);
    p.end();
    Serial.println("[eyecal] saved");
}

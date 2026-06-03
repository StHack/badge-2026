#pragma once
#include "eye_types.h"

// Physical eye cutout landmarks (screen-space pixels).
// Loaded from NVS at boot; used by eye_geometry to build Bezier curves.
extern EyeWindow gEyeCal;

void eyeCalLoad();  // called once at boot
void eyeCalSave();  // persist current gEyeCal to NVS (e.g. via BLE command)

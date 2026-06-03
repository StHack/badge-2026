#pragma once
#include "eye_types.h"

// All AlternatePupilDrawFn implementations for the eye style catalog.
// These are used by eye_catalog.cpp via function pointers.
// Each function draws only the pupil region inside the eye opening;
// the eyelid outline and brow are drawn by the caller.

extern AlternatePupilDrawFn eyeStyleHeartFn;
extern AlternatePupilDrawFn eyeStyleStarFn;
extern AlternatePupilDrawFn eyeStyleHypnosisFn;
extern AlternatePupilDrawFn eyeStyleBoltFn;
extern AlternatePupilDrawFn eyeStyleReptileFn;
extern AlternatePupilDrawFn eyeStyleInfinityShapeFn;
extern AlternatePupilDrawFn eyeStyleAnimeFn;
extern AlternatePupilDrawFn eyeStyleScopeFn;
extern AlternatePupilDrawFn eyeStyleClockFn;
extern AlternatePupilDrawFn eyeStyleRobotFn;
extern AlternatePupilDrawFn eyeStyleLockFn;
extern AlternatePupilDrawFn eyeStyleWifiFn;
extern AlternatePupilDrawFn eyeStyleCatFn;
extern AlternatePupilDrawFn eyeStyleHumanFn;

// ── Tamagotchi need — wavy iris (sick/distress) ───────────────────────────────
extern AlternatePupilDrawFn eyeStyleSickWavyFn;

#ifndef POWERSTATE_H
#define POWERSTATE_H

#include <Arduino.h>


struct PowerState {
  // --- Setpoints (Commanded by Linux) ---
  static volatile float setVoltage;
  static volatile float setCurrent; // <-- ADDED the 'static' keyword

  // --- Measurements (Probed by Arduino) ---
  static volatile float probeVoltageOutput;
  static volatile float probeCurrent;

  // --- Enable/Logic States ---
  static volatile bool internalEnable;
  static volatile bool externalEnable;
  static volatile bool outputEnabled;

  // --- Warning Lamp States ---
  static volatile bool warnLampTestState;
  static unsigned long lastWarnBlinkTimeMs;
  static bool warnLampOn;
  
  // --- Example Digital Output ---
  static volatile bool ExampleOut;
};

#endif // POWERSTATE_H

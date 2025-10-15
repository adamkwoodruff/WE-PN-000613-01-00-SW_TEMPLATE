#include "PowerState.h"

// NEW: Define and initialize the global calibration flag.
volatile bool g_calibration_received = false;

// --- Setpoints (Commanded by Linux) ---
volatile float PowerState::setVoltage = 0.0f;
volatile float PowerState::setCurrent = 0.0f; // <-- ADDED THIS LINE

// --- Measurements (Probed by Arduino) ---
volatile float PowerState::probeVoltageOutput = 0.0f;
volatile float PowerState::probeCurrent = 0.0f;

// --- Enable/Logic States ---
volatile bool PowerState::internalEnable = false;
volatile bool PowerState::externalEnable = false;
volatile bool PowerState::outputEnabled  = false;

// --- Warning Lamp States ---
volatile bool PowerState::warnLampTestState = false;
unsigned long PowerState::lastWarnBlinkTimeMs = 0;
bool PowerState::warnLampOn = false;
  
// --- Example Digital Output ---
volatile bool PowerState::ExampleOut = false;

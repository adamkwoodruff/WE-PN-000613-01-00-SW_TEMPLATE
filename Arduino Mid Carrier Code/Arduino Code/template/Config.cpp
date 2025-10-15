#include "Config.h"
#include "PowerState.h"//

// --- Define Calibration Constants ---
//
// initially bringing the system up.
float VScale_V = 1.0f;
float VOffset_V = 0.0f;
float VScale_C = 1.0f;
float VOffset_C = 0.0f;



// --- Define Timing/Threshold Constants ---
unsigned long DEBOUNCE_DELAY_US = 1000;
unsigned long WARN_BLINK_INTERVAL_MS = 5000;
float WARN_VOLTAGE_THRESHOLD = 50.0f;


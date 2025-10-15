#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>


#define APIN_VOLTAGE_PROBE   PF_11  // Analog input for voltage sensing
#define APIN_CURRENT_PROBE   A1  // Analog input for current sensing

#define DPIN_WARN_LAMP_OUT   PF_8   // Digital output for warning lamp control

// Optional hardware enable input
#define DPIN_ENABLE_IN       PF_4   // External enable input

#define DPIN_EXAMPLE_OUTPUT       PF_6 //GPIO 1

#define MEASURED_VOLT_OUT  PA_9 //PWM 1 
#define MEASURED_CURR_OUT  PA_10 //PWM 2 


// --- Calibration Constants (!!! REPLACE WITH ACTUAL VALUES !!!) ---
extern float VScale_V;
extern float VOffset_V;
extern float VScale_C;
extern float VOffset_C;

extern float VOLTAGE_PWM_FULL_SCALE; // Full-scale value used for measured voltage PWM output

// --- Debounce Parameters ---
extern unsigned long DEBOUNCE_DELAY_US; // Debounce time in microseconds

// --- Warning Lamp Parameters ---
extern unsigned long WARN_BLINK_INTERVAL_MS; // Blink half-period
extern float WARN_VOLTAGE_THRESHOLD;      // Voltage threshold for warning


// Centralized power state manager
#include "PowerState.h"


#endif // CONFIG_H
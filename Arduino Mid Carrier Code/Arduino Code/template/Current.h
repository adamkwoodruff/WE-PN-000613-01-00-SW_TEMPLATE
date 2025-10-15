#ifndef CURRENT_H
#define CURRENT_H

#include "Config.h" // Include to access pins and globals
#include "PowerState.h"

typedef int (*AnalogReadFunc)(uint8_t);
void set_current_analog_reader(AnalogReadFunc func);

// Initializes current reading components
void init_current();

// Reads ADC, scales, and updates the global actual_current
void update_current();

// RPC getter function
float get_curr_act();

#endif // CURRENT_H
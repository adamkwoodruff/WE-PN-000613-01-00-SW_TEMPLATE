#ifndef VOLTAGE_H
#define VOLTAGE_H

#include "Config.h" // Include to access pins and globals
#include "PowerState.h"

typedef int (*AnalogReadFunc)(uint8_t);
void set_voltage_analog_reader(AnalogReadFunc func);

// Initializes voltage reading components
void init_voltage();

// Reads ADC, scales, and updates the global actual_voltage
void update_voltage();

// RPC getter function
float get_volt_act();

#endif // VOLTAGE_H
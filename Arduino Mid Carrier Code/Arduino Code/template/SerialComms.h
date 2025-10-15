#ifndef SERIALCOMMS_H
#define SERIALCOMMS_H

#include <Arduino.h>
#include <string>
#include "Config.h"
#include "PowerState.h"

// Initializes the RPC library and binds all the functions
// that the Linux core can call.
void init_serial_comms();

// This function is called by the Linux core to get a packet of
// real-time data from the microcontroller.
uint64_t get_poll_data();

#endif // SERIALCOMMS_H

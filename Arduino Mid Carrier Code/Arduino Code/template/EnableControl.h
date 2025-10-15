#ifndef ENABLECONTROL_H
#define ENABLECONTROL_H

#include "Config.h" // Include to access pin definitions and global settings

// Initializes enable control pins (external enable, warn lamp)
void init_enable_control();

// Reads external enable input and updates combined outputEnable state
void update_enable_inputs();

// Updates the warning lamp output based on voltage and test flag
void update_enable_outputs();

// RPC-compatible getter for the enable state (returns 0 or 1)
int get_output_enable_state();  



#endif // ENABLECONTROL_H
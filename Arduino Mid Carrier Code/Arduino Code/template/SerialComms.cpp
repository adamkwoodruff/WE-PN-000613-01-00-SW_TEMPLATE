#include <RPC.h>
#include <ArduinoJson.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h> // Required for lroundf

#include "SerialComms.h"
#include "Voltage.h"
#include "Current.h"
#include "EnableControl.h"
#include "Config.h"
#include "PowerState.h"

/**
 * @brief Initializes the RPC system and binds the functions that can be called from Linux.
 *
 * This function sets up the communication bridge. Instead of using one generic function
 * that parses JSON, we now bind specific, typed functions for each action. This makes
 * the code much cleaner, faster, and less error-prone.
 */
void init_serial_comms() {
  RPC.begin();

  // --- Primary Data Polling ---
  // The main function used by Linux to get a continuous stream of data.
  RPC.bind("get_poll_data", get_poll_data);

  // --- NEW: Specific RPC "Set" Functions ---
  // These functions are called by Linux to command the Arduino to change a value.
  // The RPC library automatically handles the conversion from Python types.

  RPC.bind("set_volt", [](float val) {
    // Safety check: ensure voltage setpoint is not negative.
    PowerState::setVoltage = (val < 0) ? 0.0f : val;
    return true; // Acknowledge the command was received.
  });

  RPC.bind("set_curr", [](float val) {
    // Safety check: ensure current setpoint is not negative.
    PowerState::setCurrent = (val < 0) ? 0.0f : val;
    return true;
  });

  RPC.bind("set_internal_enable", [](bool enable) {
    PowerState::internalEnable = enable;
    return true;
  });

  RPC.bind("set_warn_lamp_test", [](bool test_active) {
    PowerState::warnLampTestState = test_active;
    return true;
  });
  
  RPC.bind("set_example_out", [](bool out_active) {
    PowerState::ExampleOut = out_active;
    return true;
  });
}


// --- Helper functions for data packing ---

/**
 * @brief Clamps a 32-bit signed integer to a specified range.
 */
static inline int32_t clamp_s(int32_t x, int32_t lo, int32_t hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

/**
 * @brief Masks a signed integer to its n-bit two's complement representation.
 */
static inline uint64_t mask_nbits(int32_t x, unsigned n) {
  return (uint64_t)((uint32_t)x & ((1u << n) - 1u));
}


/**
 * @brief Packs real-time data into a 64-bit integer for efficient transfer to Linux.
 *
 * This function is designed to be called repeatedly. It alternates between sending
 * two different packets of data to ensure all necessary information is synced
 * without requiring a large, single transfer.
 *
 * @return A 64-bit unsigned integer containing the packed data.
 */
uint64_t get_poll_data() {
    // Static variable to track which packet to send next. This is a common
    // technique in embedded systems to multiplex data over a single channel.
    static bool send_packet_zero = true;

    uint64_t word = 0;

    // Define the range for our 20-bit signed values: -(2^19) to (2^19 - 1)
    const int32_t max_val = (1 << 19) - 1;
    const int32_t min_val = -(1 << 19);

    if (send_packet_zero) {
        // --- PACKET 0: ID=0, Flags, Actual Measured Values ---
        // We pack boolean flags together into a single field.
        uint64_t flags = (PowerState::externalEnable ? 1U : 0U); // Start with the first flag

        // Scale and clamp our floating-point measurements to the 20-bit integer range.
        int32_t v100 = clamp_s((int32_t)lroundf(PowerState::probeVoltageOutput * 100.0f), min_val, max_val);
        int32_t c100 = clamp_s((int32_t)lroundf(PowerState::probeCurrent       * 100.0f), min_val, max_val);

        // Assemble the 64-bit word using bitwise shifts and OR operations.
        //         word |= (0ULL)                     << 63;  // Packet ID = 0 (Most significant bit)
        word |= (flags & 0x1FULL)          << 58;  // [62:58] Flags (5 bits)
        word |= (mask_nbits(v100, 20))     << 38;  // [57:38] volt_act (20 bits)
        word |= (mask_nbits(c100, 20))     << 18;  // [37:18] curr_act (20 bits)
        // [17:0] are unused in this packet

    } else {
        // --- PACKET 1: ID=1, Setpoints and other data ---
        // Scale and clamp the setpoint values.
        int32_t s100 = clamp_s((int32_t)lroundf(PowerState::setCurrent * 100.0f), min_val, max_val);
        // NOTE: Placeholder for temperature. You would have a PowerState::internalTemperature variable here.
        int32_t t100 = clamp_s((int32_t)lroundf(25.0f * 100.0f), min_val, max_val);


        word |= (1ULL)                     << 63;  // Packet ID = 1
        word |= (mask_nbits(s100, 20))     << 43;  // [62:43] curr_set (20 bits)
        word |= (mask_nbits(t100, 20))     << 23;  // [42:23] internal_temp (20 bits)
        // [22:0] are unused in this packet
    }

    // Flip the boolean so the *next* call to this function sends the other packet.
    send_packet_zero = !send_packet_zero;

    return word;
}


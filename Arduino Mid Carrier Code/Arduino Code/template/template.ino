#include <Arduino.h>
#include "Config.h"
#include "Voltage.h"
#include "Current.h"
#include "EnableControl.h"
#include "SerialComms.h"
#include <ArduinoJson.h>
#include <RPC.h>
#include <string> 
#include "SerialRPC.h"  

// --- Sync State Codes ---
#define M4_STATUS_UNKNOWN         0x0000
#define M4_STATUS_NOT_SYNCED      0xA0B0
#define M4_STATUS_SYNCHRONISING   0xA0B1
#define M4_STATUS_SYNCED          0xA0B2
#define M4_STATUS_ERROR           0xA0FF


void setup() {
  //Serial.begin(115200);

  init_serial_comms();


  init_voltage();


  init_current();


  init_enable_control();  

} 

void loop() {
  
  // --- Everything else remains as-is ---
  update_enable_inputs();
  update_voltage();
  update_current();  

  update_enable_outputs(); 

}

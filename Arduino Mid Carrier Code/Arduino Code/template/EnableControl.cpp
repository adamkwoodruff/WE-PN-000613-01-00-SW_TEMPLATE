#include "EnableControl.h"
#include "PowerState.h"

void init_enable_control() {
  pinMode(DPIN_ENABLE_IN, INPUT_PULLDOWN);    // External enable input
  // Initialize control outputs to match stored state
  pinMode(DPIN_WARN_LAMP_OUT, OUTPUT);           // Warning lamp output
  digitalWrite(DPIN_WARN_LAMP_OUT, HIGH); 

  pinMode(DPIN_EXAMPLE_OUTPUT, OUTPUT);
  digitalWrite(DPIN_EXAMPLE_OUTPUT, LOW);


}

void update_enable_inputs() {
  // Sample external enable input (debounce if needed)
  PowerState::externalEnable = (digitalRead(DPIN_ENABLE_IN) == HIGH);

  // Combined logic: both internal and external must be true
  PowerState::outputEnabled = PowerState::externalEnable && PowerState::internalEnable;
}

void update_enable_outputs() {
  unsigned long now_ms = millis();

  if (PowerState::probeVoltageOutput >= WARN_VOLTAGE_THRESHOLD) {
    // Blink the warning lamp when the output voltage is above the threshold
    if (now_ms - PowerState::lastWarnBlinkTimeMs >= WARN_BLINK_INTERVAL_MS) {
      PowerState::lastWarnBlinkTimeMs = now_ms;
      PowerState::warnLampOn = !PowerState::warnLampOn;
      digitalWrite(DPIN_WARN_LAMP_OUT, PowerState::warnLampOn ? HIGH : LOW); // does not need to be changed
    }

  } else if (PowerState::warnLampTestState) {
    // Test button pressed - force the lamp on
    PowerState::warnLampOn = true;
    digitalWrite(DPIN_WARN_LAMP_OUT, LOW);

  } else {
    // Neither condition active - ensure the lamp stays off
    PowerState::warnLampOn = false;
    digitalWrite(DPIN_WARN_LAMP_OUT, HIGH);
  } 

  digitalWrite(DPIN_EXAMPLE_OUTPUT,      PowerState::ExampleOut     ? LOW : HIGH);


}

int get_output_enable_state() {
  return PowerState::outputEnabled ? 1 : 0;
} 

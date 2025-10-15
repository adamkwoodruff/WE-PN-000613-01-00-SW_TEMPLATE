#include "SerialHandler.h"
#include <Arduino.h>

void SerialHandler::begin() {
    Serial1.begin(115200);  // Portenta UART1 for GIGA R1 communication
}

void SerialHandler::update() {
    if (Serial1.available()) {
        String command = Serial1.readStringUntil('\n');
        processCommand(command);
    }
}

void SerialHandler::sendStatus(float voltage, float current, bool warn, bool interlock, bool trigger) {
    Serial1.print("VOLT:");
    Serial1.print(voltage);
    Serial1.print(";CURR:");
    Serial1.print(current);
    Serial1.print(";WARN:");
    Serial1.print(warn ? 1 : 0);
    Serial1.print(";INT:");
    Serial1.print(interlock ? 1 : 0);
    Serial1.print(";TRIG:");
    Serial1.print(trigger ? 1 : 0);
    Serial1.println(";");
}

void SerialHandler::processCommand(const String& command) {
    if (command.startsWith("CMD:ARM")) {
        handleArmCommand();
    } else if (command.startsWith("CMD:RESET_INT")) {
        handleResetInterlock();
    } else if (command.startsWith("CMD:SET_CAL_V")) {
        handleSetCalibration(command.substring(12));
    }
}

void SerialHandler::handleArmCommand() {
    Serial.println("System Armed");
}

void SerialHandler::handleResetInterlock() {
    Serial.println("Interlock Reset");
}

void SerialHandler::handleSetCalibration(const String& payload) {
    float scale, offset;
    sscanf(payload.c_str(), "%f,%f", &scale, &offset);
    Serial.print("Calibration Set - Scale: ");
    Serial.print(scale);
    Serial.print(", Offset: ");
    Serial.println(offset);
}
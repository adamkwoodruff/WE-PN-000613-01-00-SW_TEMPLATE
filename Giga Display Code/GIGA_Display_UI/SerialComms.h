#ifndef SERIALCOMMS_H
#define SERIALCOMMS_H

#include <Arduino.h>

class SerialComms {
public:
    static void begin();
    static void update();

    static void sendCommand(const String &cmd); // if used
    static float getVoltageUpperLim();
    static float getVoltageLowerLim();
    static float getCurrentUpperLim();
    static float getCurrentLowerLim();
    static float getCpuTemp();

private:
    static void sendButtonPress(const String& name, float value, const String& doType);

    static float voltageUpperLim;
    static float voltageLowerLim;
    static float currentUpperLim;
    static float currentLowerLim;
    static float cpuTemp;
};

#endif

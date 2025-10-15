#ifndef SERIALHANDLER_H
#define SERIALHANDLER_H

#include <Arduino.h>


class SerialHandler {
public:
    void begin();
    void update();

    void sendStatus(float voltage, float current, bool warn, bool interlock, bool trigger);
    void processCommand(const String& command);

private:
    void handleArmCommand();
    void handleResetInterlock();
    void handleSetCalibration(const String& payload);
};

#endif

#ifndef USE_GIGA_UI
#include "SerialComms.h"
#include "PanelManager.h"
#include "SerialComms.h"
#include <ArduinoJson.h>

static void handleConfig(JsonObject dconf);
static void handleEvent(JsonObject ev);
static void handleStatus(JsonObject st);


float SerialComms::voltageUpperLim = 0.0f;
float SerialComms::voltageLowerLim = 0.0f;
float SerialComms::currentUpperLim = 0.0f;
float SerialComms::currentLowerLim = 0.0f;

// Add a new static variable to store CPU temperature
float SerialComms::cpuTemp = 0.0f;  // Degrees Celsius

void SerialComms::begin() {
    Serial1.begin(115200); // Linux Portenta UART
    Serial2.begin(115200); // Portenta Arduino side

    // Request config on startup
    Serial1.println(R"({"display_event":{"type":"get","action":"config"}})");
}


void SerialComms::update() {
    while (Serial1.available()) {
        String message = Serial1.readStringUntil('\n');
        DynamicJsonDocument doc(52000);
        DeserializationError error = deserializeJson(doc, message);

        if (error) {
            Serial.println("JSON parse error:");
            Serial.println(message);
            Serial.println(error.c_str());
            continue;
        }

        if (doc.containsKey("display_config")) {
            handleConfig(doc["display_config"]);
        }
        if (doc.containsKey("display_event")) {
            handleEvent(doc["display_event"]);
        }
        if (doc.containsKey("display_status")) {
            handleStatus(doc["display_status"]);
        }
    }
}

static void handleConfig(JsonObject dconf) {
    String ver = dconf["version"] | "1.0";
    if (ver == "1.1") {
        PanelManager::reset();
        bool ok = PanelManager::parsePanels(dconf);
        if (ok) {
            Serial.println("[SerialComms] v1.1 config loaded successfully");
        } else {
            Serial.println("[SerialComms] v1.1 config parse error");
        }
    } else {
        Serial.println("[SerialComms] Old or unknown version, fallback logic...");
    }
}

static void handleEvent(JsonObject ev) {
    String type = ev["type"] | "";
    String name = ev["name"] | "";
    float val   = ev["value"] | 0.0f;

    if (type == "get_value") {
        bool found = false;
        float cur = PanelManager::getValueByName(name, found);
        StaticJsonDocument<256> resp;
        JsonObject re = resp.createNestedObject("display_event");
        re["type"] = "get_value_response";
        re["name"] = name;
        if (!found) {
            re["code"] = "fail_not_found";
        } else {
            re["value"] = cur;
            re["code"]  = "ok";
        }
        String out;
        serializeJson(resp, out);
        Serial1.println(out);
    } else if (type == "set_value") {
        bool success = PanelManager::setValueByName(name, val);
        StaticJsonDocument<256> resp;
        JsonObject re = resp.createNestedObject("display_event");
        re["type"] = "set_value_response";
        re["name"] = name;
        re["value"] = val;
        re["code"]  = success ? "ok" : "fail_not_found";
        String out;
        serializeJson(resp, out);
        Serial1.println(out);
    } else if (type == "status_report") {
        handleStatus(ev);
    }
}


void SerialComms::sendButtonPress(const String& name, float value, const String& doType) {
    StaticJsonDocument<256> doc;
    JsonObject ev = doc.createNestedObject("display_event");
    ev["type"] = "button_press";
    ev["name"] = name;
    ev["value"] = value;
    ev["do"] = doType;
    ev["dest"] = "linux";
    serializeJson(doc, Serial1);
    Serial1.println();
}




// (If you want to retrieve cpuTemp from GIGA_Display_UI.ino)
float SerialComms::getCpuTemp() {
    return cpuTemp;
} 

#endif
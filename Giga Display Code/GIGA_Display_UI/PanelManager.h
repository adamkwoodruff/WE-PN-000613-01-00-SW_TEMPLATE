#ifndef PANELMANAGER_H
#define PANELMANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <string> // Include std::string for potential internal use if needed

// Panel types enumeration
enum PanelType {
    PANEL_UNKNOWN,
    PANEL_MENU,
    PANEL_CONTROL,
    PANEL_TEXT
};

// Action definition for buttons
struct ButtonAction {
    String name;    // e.g. "set_v"
    String doType;  // e.g. "add", "subtract", "set", "mult", "toggle"
    float amount;   // e.g. 10.0
};

// Button definition
struct ButtonDef {
    int id;
    bool visible; //
    bool disable; //
    ButtonAction action;
    String text;        // e.g. "V+"
    String eventDest;   // "uc" or "linux"
};

// Label definition
struct LabelDef {
    int id;
    bool visible; //
    String text;        // e.g. "Voltage:"
    String color;       // e.g. "white"
};

// Value definition
struct ValueDef {
    int id; 
    int displayId;
    String name;         // e.g. "set_v"
    String type;         // "float" or "string"
    String displayFormat; //
    float maxVal; //
    float minVal; //
    float upperGreenVal; //
    float lowerRedVal; //
    float upperOverrideVal; //
    String upperOverrideText; //
    float lowerOverrideVal; //
    String lowerOverrideText; //
    String defaultColor; //
    float defaultValue; //
    float currentValue; // Current value for UI reference
};

// Panel definition
struct PanelDef {
    int id;
    PanelType type;
    String title; //
    std::vector<ButtonDef> buttons; //
    std::vector<LabelDef> labels; //
    std::vector<ValueDef> values; //
};

// PanelManager class definition
class PanelManager {
public:
    static void reset(); //
    // Parses the "panels" array from the provided JSON config object
    static bool parsePanels(JsonObject& displayConfig); //
    // Retrieves a panel definition by its ID
    static PanelDef* getPanelById(int pid); //
    // Sets a named value (e.g., "set_v") across all panel definitions
    static bool setValueByName(const String& name, float newVal); //
    // Gets a named value across all panel definitions
    static float getValueByName(const String& name, bool& found); //
    // Checks if a given panel contains a value by name
    static bool panelHasValue(int pid, const String& name); //
    // Converts color name string to a 16-bit color value
    static uint16_t parseColor(const String &colorName); //

    // Static vector holding all parsed panel definitions
    static std::vector<PanelDef> panels; //

private:
    // Converts panel type string to PanelType enum
    static PanelType stringToPanelType(const String& sType); //
};

#endif // PANELMANAGER_H
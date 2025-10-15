#include "PanelManager.h"
#include <ArduinoJson.h>

std::vector<PanelDef> PanelManager::panels;

void PanelManager::reset() {
    panels.clear();
}

bool PanelManager::parsePanels(JsonObject& displayConfig) {
    if (!displayConfig.containsKey("panels")) {
        Serial.println("[PanelManager] Error: 'panels' key not found in config.");
        return false;
    }

    JsonArray panelsArray = displayConfig["panels"].as<JsonArray>();
    if (panelsArray.isNull()) {
        Serial.println("[PanelManager] Error: 'panels' is not an array.");
        return false;
    }

    reset();

    for (JsonVariant panelVar : panelsArray) {
        if (!panelVar.is<JsonObject>()) continue;
        JsonObject pObj = panelVar.as<JsonObject>();
        PanelDef pd;
        pd.id = pObj["id"] | -1;

        if (pObj.containsKey("title") && pObj["title"].is<const char*>()) {
            pd.title = pObj["title"].as<String>();
        } else {
            pd.title = "Panel " + String(pd.id);
        }

        pd.type = stringToPanelType(pObj["type"] | "unknown");

        // Buttons
        if (pObj.containsKey("buttons")) {
            for (JsonObject bObj : pObj["buttons"].as<JsonArray>()) {
                ButtonDef bd;
                bd.id = bObj["id"].as<int>();
                bd.visible = bObj["visible"] | true;
                bd.disable = bObj["disable"] | false;
                // *** THIS IS THE CORRECTED LINE ***
                bd.text = bObj["text"] | String("Button " + String(bd.id));
                bd.eventDest = bObj["event_dest"] | String("uc");

                if (bObj.containsKey("action") && bObj["action"].is<JsonObject>()) {
                    JsonObject aObj = bObj["action"];
                    bd.action.name = aObj["name"] | "";
                    bd.action.doType = aObj["do"] | "";
                    bd.action.amount = aObj["amount"] | 0.0f;
                }

                pd.buttons.push_back(bd);
            }
        }

        // Labels
        if (pObj.containsKey("labels")) {
            for (JsonObject lObj : pObj["labels"].as<JsonArray>()) {
                LabelDef ld;
                ld.id = lObj["id"].as<int>();
                ld.text = lObj["text"] | "";
                ld.visible = lObj["visible"] | true;
                ld.color = lObj["color"] | "white";
                pd.labels.push_back(ld);
            }
        }

        // Values
        if (pObj.containsKey("values")) {
            for (JsonObject vObj : pObj["values"].as<JsonArray>()) {
                ValueDef vd;
                vd.id = vObj["id"].as<int>();

                if (vObj.containsKey("name") && vObj["name"].is<const char*>()) {
                    vd.name = vObj["name"].as<String>();
                } else {
                    vd.name = "val_" + String(vd.id);
                }

                vd.type = vObj["type"] | "float";
                vd.displayFormat = vObj["display_format"] | (vd.type == "float" ? "%.2f" : "%d");

                vd.maxVal = vObj["max_val"] | 999999.0f;
                vd.minVal = vObj["min_val"] | -999999.0f;
                vd.upperGreenVal = vObj["upper_green_val"] | vd.maxVal;
                vd.lowerRedVal = vObj["lower_red_val"] | vd.minVal;
                vd.upperOverrideVal = vObj["upper_override_val"] | (vd.maxVal + 1.0f);
                vd.lowerOverrideVal = vObj["lower_override_val"] | (vd.minVal - 1.0f);

                vd.upperOverrideText = vObj["upper_override_text"] | "";
                vd.lowerOverrideText = vObj["lower_override_text"] | "";
                vd.defaultColor = vObj["default_color"] | "white";
                if (vObj.containsKey("default_value")) {
                    vd.defaultValue = vObj["default_value"];
                } else if (vd.type == "bool") {
                    vd.defaultValue = 0.0f;  // default to false for bools
                } else {
                    vd.defaultValue = vd.minVal;
                }

                vd.currentValue = vd.defaultValue;

                vd.displayId = vObj["display_id"] | vd.id;

                pd.values.push_back(vd);
            }
        }

        panels.push_back(pd);
    }

    return true;
}

PanelDef* PanelManager::getPanelById(int pid) {
    for (auto &p : panels) {
        if (p.id == pid) return &p;
    }
    return nullptr;
}

bool PanelManager::setValueByName(const String& name, float newVal) {
    bool found = false;
    for (auto &p : panels) {
        for (auto &v : p.values) {
            if (v.name == name) {
                found = true;
                float val = newVal;

                Serial.print("[PanelManager] setValueByName → ");
                Serial.print(name);
                Serial.print(" | Raw: ");
                Serial.print(newVal);
                Serial.print(" | Constrained: ");
                Serial.println(val);

                if (fabs(v.currentValue - val) > 0.0001f) {
                    v.currentValue = val;
                } else {
                    Serial.println("[PanelManager] Value unchanged.");
                }

                break;
            }
        }
        if (found) break;
    }

    if (!found) {
        Serial.print("[PanelManager] ❌ No value found for name: ");
        Serial.println(name);
    }

    return found;
}

float PanelManager::getValueByName(const String& name, bool& found) {
    for (auto &p : panels) {
        for (auto &v : p.values) {
            if (v.name == name) {
                found = true;
                return v.currentValue;
            }
        }
    }
    found = false;
    return 0.0f;
}

bool PanelManager::panelHasValue(int pid, const String& name) {
    PanelDef* p = getPanelById(pid);
    if (!p) return false;
    for (auto &v : p->values) {
        if (v.name == name) return true;
    }
    return false;
}

PanelType PanelManager::stringToPanelType(const String& sType) {
    if (sType.equalsIgnoreCase("menu"))    return PANEL_MENU;
    if (sType.equalsIgnoreCase("control")) return PANEL_CONTROL;
    if (sType.equalsIgnoreCase("text"))    return PANEL_TEXT;
    return PANEL_UNKNOWN;
}

uint16_t PanelManager::parseColor(const String& colorName) {
    String c = colorName;
    c.toLowerCase();
    if (c == "red") return 0xF800;
    if (c == "green") return 0x07E0;
    if (c == "blue") return 0x001F;
    if (c == "yellow") return 0xFFE0;
    if (c == "cyan") return 0x07FF;
    if (c == "magenta") return 0xF81F;
    if (c == "white") return 0xFFFF;
    if (c == "black") return 0x0000;
    if (c == "gray") return 0x8410;
    if (c == "orange") return 0xFD20;
    return 0xFFFF;
}

#include <Arduino.h>
#include <Arduino_GigaDisplayTouch.h>
#include "Arduino_H7_Video.h"
#include "lvgl.h"
#include <vector>
#include <string>
#include <ArduinoJson.h>
#include <math.h>
#include "PanelManager.h"


static int g_currentPanelId = 0;
static bool configLoaded = false;
static int lastRenderedPanelId = -1;
static unsigned long lastConfigRequestTime = 0;

// --- Loading Status UI ---
static String currentStatus = "";
static lv_obj_t *statusLabel = nullptr;
static lv_obj_t *statusBar = nullptr;
static lv_obj_t *detailLabel = nullptr;
static int statusStage = 0; // 0-5

// Track and display whether the system is in local or remote control mode.
static String currentMode = "Local";
static lv_obj_t *modeLabel = nullptr;


Arduino_GigaDisplayTouch touchDetector;
Arduino_H7_Video Display(800, 480, GigaDisplayShield); 

struct Rect {
    int x, y, w, h;
    int btnId;
};

static std::vector<Rect> g_buttonRects;
lv_obj_t *screen;

static ButtonDef pendingButtonAction;
static bool pendingActionActive = false;

// Simple time based debounce for touch presses
static unsigned long lastButtonPressTime = 0;
static const unsigned long buttonDebounceDelay = 200; // milliseconds

void sendSetValueEvent(const ButtonDef& b, float newVal);

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
void renderCurrentPanel();
void renderMenuPanel(const PanelDef &panel);
void renderControlPanel(const PanelDef &panel);
void renderTextPanel(const PanelDef &panel);
void handleTouchCoord(uint16_t x, uint16_t y);
void onButtonPressed(int btnId);
void applyButtonAction(const ButtonDef &b);
void requestConfigFromLinux();
void processIncomingMessage(const String& message);

void on_back_button(lv_event_t *e) {
    Serial.println("[UI] Back button pressed via LVGL");
    g_currentPanelId = 0;
    lastRenderedPanelId = -1;
}

void my_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    // Not implemented
}

float wave_phase = 0.0;
unsigned long last_wave_time = 0;

void setup() {
    Serial.begin(115200);
    Serial1.begin(115200);

    Display.begin();
    touchDetector.begin();

    lv_init();
    lv_display_t *disp = lv_display_create(800, 480);
    lv_display_set_flush_cb(disp, my_flush_cb);

    screen = lv_obj_create(NULL);
    lv_scr_load(screen);

    lv_obj_clean(screen);

    statusLabel = lv_label_create(screen);
    lv_label_set_text(statusLabel, "Waiting for docker to start...");
    lv_obj_align(statusLabel, LV_ALIGN_CENTER, 0, -10);

    statusBar = lv_bar_create(screen);
    lv_obj_set_size(statusBar, 300, 20);
    lv_obj_align(statusBar, LV_ALIGN_CENTER, 0, 20);
    lv_bar_set_range(statusBar, 0, 4);
    lv_bar_set_value(statusBar, 0, LV_ANIM_OFF);

    detailLabel = lv_label_create(screen);
    lv_label_set_text(detailLabel, "");
    lv_obj_align(detailLabel, LV_ALIGN_CENTER, 0, 50);
    currentStatus = "Docker starting";


    Serial.println("[UI] Setup complete. Requesting config...");
    requestConfigFromLinux();
    lastConfigRequestTime = millis();
}

void loop() {

    while (Serial1.available()) {
        String message = Serial1.readStringUntil('\n');
        Serial.print("[UI] Received from Linux: ");
        Serial.println(message);
        processIncomingMessage(message);
    }

    if (!configLoaded && millis() - lastConfigRequestTime >= 1000) {
        requestConfigFromLinux();
        lastConfigRequestTime = millis();
    }


    if (configLoaded && lastRenderedPanelId != g_currentPanelId) {
        renderCurrentPanel();
        lastRenderedPanelId = g_currentPanelId;
    }

    GDTpoint_t points[5];
    uint8_t contacts = touchDetector.getTouchPoints(points);
    if (contacts > 0) {
        uint16_t x = points[0].y;
        uint16_t y = 480 - points[0].x;
        handleTouchCoord(x, y);
        delay(1);
    }

    lv_timer_handler();
    delay(1);
}

// --- Function Implementations ---

void requestConfigFromLinux() {
    // Send request to Linux via Serial1
    Serial1.println(R"({"display_event":{"type":"get","action":"config"}})");
    Serial.println("[UI] Sent config request to Linux.");
}

void processIncomingMessage(const String& message) {
    // Attempt to parse the incoming JSON message
    DynamicJsonDocument doc(128000);  // ~125 KB // Use larger buffer for config
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
        Serial.print("[UI] JSON parsing failed: ");
        Serial.println(error.c_str());
        return;
    }

    // Check if it's a configuration message
    if (doc.containsKey("display_config")) {
        JsonObject dconf = doc["display_config"].as<JsonObject>();
        String ver = dconf["version"] | "unknown";
        Serial.print("[UI] Received config version: ");
        Serial.println(ver);

        // Call PanelManager to parse the panels section
        bool parse_ok = PanelManager::parsePanels(dconf); // Pass the config object

        if (parse_ok && PanelManager::panels.size() > 0) { // [cite: 11] condition check
            Serial.println("[UI] Config parsed successfully by PanelManager.");
            configLoaded = true; // [cite: 11]
            g_currentPanelId = 0; // Start at menu/first panel [cite: 11]
            lastRenderedPanelId = -1; // Force re-render [cite: 12]

        } else {
            Serial.println("[UI] Config parsing failed or no panels found.");
            // Optionally display an error on the screen
            lv_obj_clean(screen);
            lv_obj_t *errLabel = lv_label_create(screen);
            lv_label_set_text(errLabel, "Error loading configuration!");
            lv_obj_align(errLabel, LV_ALIGN_CENTER, 0, 0);
            configLoaded = false;
        }
    }
    // Check for status updates
    else if (doc.containsKey("display_status")) {
        JsonObject st = doc["display_status"].as<JsonObject>();
        String stage = st["stage"] | "";
        String detail = st["detail"] | "";
        if (stage.length() > 0) {
            currentStatus = stage;
            if (statusLabel) {
                lv_label_set_text(statusLabel, stage.c_str());
            }
            if (statusBar) {
                if (stage == "Docker starting") statusStage = 0;
                else if (stage == "Building container") statusStage = 1;
                else if (stage == "Container ready") statusStage = 2;
                else if (stage == "Waiting for config") statusStage = 3;
                else if (stage == "Bridge running") statusStage = 4;
                lv_bar_set_value(statusBar, statusStage, LV_ANIM_OFF);
            }
        }
        if (detail.length() > 0 && detailLabel) {
            lv_label_set_text(detailLabel, detail.c_str());
        }
    }
    // Check if it's an event to set a value
    else if (doc.containsKey("display_event")) {
         JsonObject ev = doc["display_event"].as<JsonObject>();
         String type = ev["type"] | "";
         String name = ev["name"] | "";

         if (type == "set_value") {
            // Make sure name exists before trying to get value
            if (name.length() > 0) {
                float val = ev["value"] | 0.0f; // Default to 0 if value missing/invalid
                bool found = false;
                float oldVal = PanelManager::getValueByName(name, found);
                bool success = found;
                if (found && fabs(oldVal - val) > 0.0001f) {
                    PanelManager::setValueByName(name, val);
                }
                Serial.print("[UI] Set value '"); Serial.print(name);
                Serial.print("' to "); Serial.print(val);
                Serial.println(success ? " OK" : " FAILED (Not Found?)");

                if (name == "mode_set") {
                    currentMode = (val >= 0.5f) ? "Remote" : "Local";
                    if (modeLabel) {
                        String modeText = String("Mode: ") + currentMode;
                        lv_label_set_text(modeLabel, modeText.c_str());
                    }
                }


                // Re-render only if the current panel uses this value and it changed
                if (success && fabs(oldVal - val) > 0.0001f &&
                    configLoaded && PanelManager::panelHasValue(g_currentPanelId, name)) {
                    lastRenderedPanelId = -1; // force re-render
                }

             if (pendingActionActive && pendingButtonAction.action.name == name) {
                  // Send raw button action directly with no computed value
                  StaticJsonDocument<256> doc;
                  JsonObject ev = doc.createNestedObject("display_event");
                  ev["type"]  = "button_press";
                  ev["name"]  = pendingButtonAction.action.name;
                  ev["value"] = val;
                  ev["dest"]  = pendingButtonAction.eventDest;
                  ev["do"]    = pendingButtonAction.action.doType;

                  String out;
                  serializeJson(doc, out);
                  Serial1.println(out);
                  Serial.print("[UI] Sent raw button event (deferred): ");
                  Serial.println(out);
                  pendingActionActive = false;
              }

            } else {
                 Serial.println("[UI] Received set_value event with empty name.");
            }
         }
         // Handle other event types from Linux/uc if needed
         else {
             Serial.print("[UI] Received unhandled display_event type: ");
             Serial.println(type);
         }
    } else {
        Serial.println("[UI] Received JSON message with unknown top-level key.");
    }
}


void handleTouchCoord(uint16_t x, uint16_t y) {
    unsigned long now = millis();
    if (now - lastButtonPressTime < buttonDebounceDelay) {
        return; // debounce: ignore if pressed too soon after last press
    }
    // Check if touch is within any defined button rectangle
    for (const auto &r : g_buttonRects) {
        if (x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h)) {
            onButtonPressed(r.btnId);
            lastButtonPressTime = now;
            return; // Process only the first hit
        }
    }
}

void onButtonPressed(int btnId) {
    Serial.print("[UI] Button pressed: ID ");
    Serial.println(btnId);

    PanelDef *p = PanelManager::getPanelById(g_currentPanelId); // [cite: 21]
    if (!p) {
        Serial.println("[UI] Error: Current panel not found!");
        return;
    }

    // Find the button definition in the current panel
    for (const auto &b : p->buttons) { // [cite: 22]
        if (b.id == btnId) { // [cite: 22]
            if (b.disable) { // [cite: 22]
                 Serial.println("[UI] Button disabled.");
                 return;
            }
            applyButtonAction(b); // [cite: 23]
            return;
        }
    }
     Serial.println("[UI] Button ID not found in current panel.");
}

void applyButtonAction(const ButtonDef &b) {
    Serial.print("[UI] Applying action for button '");
    Serial.print(b.text);
    Serial.print("', action name: '");
    Serial.print(b.action.name);
    Serial.print("', destination: '");
    Serial.print(b.eventDest);
    Serial.println("'");

    // 0) Previous remote/local lock logic removed

    // 2) Handle panel navigation (always allowed)
    if (b.action.name == "display_panel_id" && b.action.doType == "set") {
        g_currentPanelId = int(b.action.amount);
        Serial.print("[UI] Navigating to panel ID: ");
        Serial.println(g_currentPanelId);
        lastRenderedPanelId = -1;
        return;  // no RPC event for navigation
    }

    // 3) If destination is Linux, request a fresh value instead of using cache
    if (b.eventDest == "linux") {
        if (pendingActionActive) {
            Serial.println("[UI] Pending action in progress, ignoring press.");
            return;
        }
        pendingButtonAction = b;
        pendingActionActive = true;
        StaticJsonDocument<256> req;
        JsonObject ev = req.createNestedObject("display_event");
        ev["type"] = "get_value";
        ev["name"] = b.action.name;
        String out;
        serializeJson(req, out);
        Serial1.println(out);
        Serial.print("[UI] Requested current value for ");
        Serial.println(b.action.name);
        return;
    }

     // 4) Prepare direct event to Linux (no local math)
    float valueForEvent = b.action.amount;


    // 5) Send the RPC event to Linux
    StaticJsonDocument<500> doc;
    JsonObject ev = doc.createNestedObject("display_event");
    ev["type"]  = "button_press";
    ev["name"]  = b.action.name;
    if (b.action.doType == "get") {
        ev["value"] = 0;  // prevent rounding corruption
    } else {
        ev["value"] = valueForEvent;
    }

    ev["dest"]  = b.eventDest;
    ev["do"]    = b.action.doType;

    Serial.print("[UI] Event name: ");
    Serial.print(b.action.name);
    Serial.print(", value: ");
    Serial.println(valueForEvent);

    String out;
    serializeJson(doc, out);
    Serial1.println(out);
    Serial.print("[UI] Sent event to Linux: ");
    Serial.println(out);
}


// --- Panel Rendering Functions ---

void renderCurrentPanel() {
    PanelDef *panel = PanelManager::getPanelById(g_currentPanelId); // [cite: 29]
    if (!panel) {
        Serial.print("[UI] Error: Panel ID "); Serial.print(g_currentPanelId); Serial.println(" not found for rendering.");
        // Display an error message on screen
        lv_obj_clean(screen); // [cite: 29]
        lv_obj_t *errLabel = lv_label_create(screen);
        lv_label_set_text_fmt(errLabel, "Error: Panel %d not found", g_currentPanelId);
        lv_obj_align(errLabel, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    Serial.print("[UI] Rendering Panel ID: "); Serial.print(panel->id);
    Serial.print(", Title: "); Serial.println(panel->title);

    lv_obj_clean(screen); // Clear previous widgets [cite: 29]
    g_buttonRects.clear(); // Clear old button regions for touch detection [cite: 31]

    lv_obj_t *titleBar = lv_obj_create(screen);
    lv_obj_set_size(titleBar, 800, 40);
    lv_obj_align(titleBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(titleBar, lv_color_hex(0x323232), 0);
    lv_obj_set_style_radius(titleBar, 0, 0);
    lv_obj_set_style_border_width(titleBar, 0, 0);

    // Title text
    lv_obj_t *titleLabel = lv_label_create(titleBar);
    lv_label_set_text(titleLabel, panel->title.c_str());
    lv_obj_align(titleLabel, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);

    // Display the current control mode on the title bar so the operator can
    // easily see whether inputs are coming from the Giga screen or the remote
    // UDP interface.
    modeLabel = lv_label_create(titleBar);
    String modeText = String("Mode: ") + currentMode;
    lv_label_set_text(modeLabel, modeText.c_str());
    lv_obj_set_style_text_color(modeLabel, lv_color_white(), 0);
    lv_obj_align(modeLabel, LV_ALIGN_RIGHT_MID, -100, 0);

    // --- Back Button (if not on menu panel, assuming menu is ID 0) ---
    if (g_currentPanelId != 0) { // [cite: 36] condition
        lv_obj_t *backBtn = lv_btn_create(titleBar); // [cite: 36]
        lv_obj_set_size(backBtn, 80, 30); // [cite: 37]
        lv_obj_align(backBtn, LV_ALIGN_RIGHT_MID, -10, 0); // [cite: 37]
        // Use LVGL event system for LVGL buttons
        lv_obj_add_event_cb(backBtn, on_back_button, LV_EVENT_CLICKED, NULL); // [cite: 37]
        lv_obj_t *backLabel = lv_label_create(backBtn); // [cite: 37]
        // lv_label_set_text(backLabel, LV_SYMBOL_LEFT " Menu"); // Use built-in symbol
        lv_label_set_text(backLabel, "Menu"); // Simple text [cite: 37]
        lv_obj_center(backLabel);
        // lv_obj_set_style_text_color(backLabel, lv_color_black(), 0); // Set text color [cite: 37] - style button instead if needed
    }

    // --- Render Panel Content based on type ---
    switch (panel->type) { // [cite: 32]
        case PANEL_MENU:
            renderMenuPanel(*panel); // [cite: 32]
            break;
        case PANEL_CONTROL:
            renderControlPanel(*panel); // [cite: 33]
            break;
        case PANEL_TEXT:
            renderTextPanel(*panel); // [cite: 34]
            break;
        default: // [cite: 35]
            // Handle unknown panel type
            lv_obj_t *errLabel = lv_label_create(screen);
            lv_label_set_text(errLabel, "Unknown Panel Type");
            lv_obj_align(errLabel, LV_ALIGN_CENTER, 0, 50);
            break; // [cite: 35]
    }
     Serial.println("[UI] Panel rendering complete.");
}


void renderControlPanel(const PanelDef &panel) {
    Serial.println("[UI] Rendering Control Panel (Split Layout)...");

    // --- Left Side: Button Grid Parameters ---
    const int btnGridCols    = 2;
    const int btnGridRows    = 4;
    const int btnW           = 180;
    const int btnH           = 90;
    const int btnColSpacing  = 15;
    const int btnRowSpacing  = 10;
    const int btnStartX      = 10;
    const int btnStartY      = 50; // Below title bar + padding
    int mainBtnCount         = 0;  // Counter for main buttons on the left

    // --- Right Side: Data Grid Parameters ---
    const int dataRows       = 8;
    const int dataRowHeight  = 53;
    const int dataStartY     = 50; // Align top with buttons
    const int label1StartX   = 410;
    const int labelWidth     = 90;
    const int valueStartX    = 510; // label1StartX + labelWidth + 10
    const int valueWidth     = 90;
    const int editBtnStartX  = 610; // valueStartX + valueWidth + 10
    const int editBtnWidth   = 60;
    const int editBtnHeight  = 35;  // Smaller than row height
    const int label2StartX   = 680; // editBtnStartX + editBtnWidth + 10
    const int label2Width    = 110; // Fill remaining space (800 - 680 - 10)
    const int elementVPad    = (dataRowHeight - editBtnHeight) / 2; 

    // Clear any old touch‐rectangles
    g_buttonRects.clear();

    // 1) Render the main “buttons” on the left
    for (const auto &b : panel.buttons) {
        if (!b.visible) continue;
        if (b.id >= 100 && b.id < 200) continue;    // skip “edit” buttons here
        if (mainBtnCount >= btnGridCols * btnGridRows) break;

        int col = mainBtnCount % btnGridCols;
        int row = mainBtnCount / btnGridCols;
        int x   = btnStartX + col * (btnW + btnColSpacing);
        int y   = btnStartY + row * (btnH + btnRowSpacing);

        lv_obj_t *btn = lv_btn_create(screen);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, btnW, btnH);
        lv_obj_set_style_radius(btn, 5, 0);

        g_buttonRects.push_back({ x, y, btnW, btnH, b.id });

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, b.text.c_str());
        lv_obj_set_width(lbl, btnW - 10);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_center(lbl);

        mainBtnCount++;
    }


    // 2) Render the data grid on the right
    for (int row = 0; row < dataRows; ++row) {
        int yTop       = dataStartY + row * dataRowHeight;
        int centerY    = yTop + dataRowHeight/2;
        int editBtnY   = yTop + elementVPad;

        // Column 1: Label
        int lab1Id = row + 1;
        for (auto &L : panel.labels) {
            if (L.visible && L.id == lab1Id) {
                lv_obj_t *nameLabel = lv_label_create(screen);
                lv_label_set_text(nameLabel, L.text.c_str());
                lv_obj_set_style_text_color(nameLabel, lv_color_black(), 0);
                lv_obj_set_width(nameLabel, labelWidth);
                lv_obj_align(nameLabel, LV_ALIGN_TOP_LEFT, label1StartX, centerY - lv_obj_get_height(nameLabel)/2);
                break;
            }
        }

        // Column 2: Value
        int displaySlot = row + 1; // Using display_id now instead of id
        for (auto &V : panel.values) {
            if (V.displayId == displaySlot) {
                lv_obj_t *valueLabel = lv_label_create(screen);
                uint16_t colorHex = PanelManager::parseColor("black");
                bool found = false;
                float cur = PanelManager::getValueByName(V.name, found);
                if (!found) continue;

                char buf[32];

                // Special case for SW_GET_VERSION
                if (V.name == "SW_GET_VERSION") {
                    uint32_t raw = static_cast<uint32_t>(cur);
                    char prefix[3] = {
                        static_cast<char>((raw >> 24) & 0xFF),
                        static_cast<char>((raw >> 16) & 0xFF),
                        '\0'
                    };
                    int ver = (raw >> 8) & 0xFF;
                    int iface = raw & 0xFF;
                    snprintf(buf, sizeof(buf), "%s%02d%02d", prefix, ver, iface);  // → "WE0301"
                } else {
                    snprintf(buf, sizeof(buf), V.displayFormat.c_str(), cur);
                }

                lv_label_set_text(valueLabel, buf);


                if (cur > V.upperOverrideVal) {
                    colorHex = PanelManager::parseColor("red");
                } else if (cur < V.lowerOverrideVal) {
                    colorHex = PanelManager::parseColor("blue");
                }

                lv_obj_set_style_text_color(valueLabel, lv_color_hex(colorHex), 0);
                lv_obj_set_width(valueLabel, valueWidth);
                lv_obj_align(valueLabel, LV_ALIGN_TOP_LEFT, valueStartX, centerY - lv_obj_get_height(valueLabel)/2);
                break;
            }
        }

        // Column 3: Edit Button (same as before)
        int editId = 101 + row;
        for (auto &b : panel.buttons) {
            if (b.visible && b.id == editId) {
                lv_obj_t *eb = lv_btn_create(screen);
                lv_obj_set_pos(eb, editBtnStartX, editBtnY);
                lv_obj_set_size(eb, editBtnWidth, editBtnHeight);
                g_buttonRects.push_back({ editBtnStartX, editBtnY, editBtnWidth, editBtnHeight, b.id });
                lv_obj_t *el = lv_label_create(eb);
                lv_label_set_text(el, b.text.c_str());
                lv_obj_set_width(el, editBtnWidth - 10);
                lv_label_set_long_mode(el, LV_LABEL_LONG_WRAP);
                lv_obj_center(el);
                break;
            }
        }

        // Column 4: Right-side Label
        int lab2Id = row + 1 + dataRows;
        for (auto &L : panel.labels) {
            if (L.visible && L.id == lab2Id) {
                lv_obj_t *lab2 = lv_label_create(screen);
                lv_label_set_text(lab2, L.text.c_str());
                lv_obj_set_style_text_color(lab2, lv_color_black(), 0);
                lv_obj_set_width(lab2, label2Width);
                lv_obj_align(lab2, LV_ALIGN_TOP_LEFT, label2StartX, centerY - lv_obj_get_height(lab2)/2);
                break;
            }
        }
    }
    

    Serial.println("[UI] Control Panel rendering complete.");
}



// --- Optional: Update renderMenuPanel ---
// If you want menu panels to ONLY show the left-side 2x4 grid and no data:
void renderMenuPanel(const PanelDef &panel) {
    Serial.println("[UI] Rendering Menu Panel (2x4 Button Grid)...");

    // --- Button Grid Parameters (Same as Control Panel Left Side) ---
    const int btnGridCols = 2;
    const int btnGridRows = 4;
    const int btnW = 180;
    const int btnH = 100;
    const int btnColSpacing = 15;
    const int btnRowSpacing = 10;
    const int btnStartX = 10;
    const int btnStartY = 50;
    int btnCount = 0;

    g_buttonRects.clear(); // Clear touch rects

    for (const auto &button : panel.buttons) {
        if (!button.visible) continue;

        // Stop if we've filled the grid
        if (btnCount >= btnGridCols * btnGridRows) {
            Serial.println("[UI] Warning: More buttons defined than fit in the 2x4 grid. Stopping.");
            break;
        }

        // Calculate grid position
        int col = btnCount % btnGridCols;
        int row = btnCount / btnGridCols;
        int x = btnStartX + col * (btnW + btnColSpacing);
        int y = btnStartY + row * (btnH + btnRowSpacing);

        // Create LVGL Button
        lv_obj_t * btn = lv_btn_create(screen);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_size(btn, btnW, btnH);
        lv_obj_set_style_radius(btn, 5, 0);
        g_buttonRects.push_back({x, y, btnW, btnH, button.id});

        // Create Button Label
        lv_obj_t * label = lv_label_create(btn);
        lv_label_set_text(label, button.text.c_str());
        lv_obj_set_width(label, btnW - 10);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_center(label);

        btnCount++;
    }
    Serial.println("[UI] Menu Panel rendering complete.");
}

// Render a text panel (mostly labels)
void renderTextPanel(const PanelDef &panel) {
    Serial.println("[UI] Rendering Text Panel...");
    int textY = 60; // Start Y position [cite: 72]
    int textX = 40; // Start X position
    int lineSpacing = 25; // Spacing between lines
    int maxW = 720; // Max width for text wrapping

    for (const auto &lbl : panel.labels) { // [cite: 73]
        if (!lbl.visible) continue; // [cite: 73]
        lv_obj_t *lab = lv_label_create(screen); // [cite: 74]
        lv_label_set_text(lab, lbl.text.c_str()); // [cite: 74]
        lv_obj_set_style_text_color(lab, lv_color_black(), 0); // [cite: 74]
        lv_obj_set_width(lab, maxW); // Enable text wrapping
        lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP); // Set wrap mode
        lv_obj_align(lab, LV_ALIGN_TOP_LEFT, textX, textY); // [cite: 74]

        // Adjust Y position for the next label based on the height of the current one
        lv_area_t label_coords;
        lv_obj_get_coords(lab, &label_coords);
        textY = label_coords.y2 + lineSpacing; // Move below the current label [cite: 74] related logic

        if (textY > 460) { // Stop if running out of vertical space
            Serial.println("[UI] Warning: Text panel content exceeds screen height.");
            break;
        }
    }
    // Add rendering for any buttons defined for the text panel here
}
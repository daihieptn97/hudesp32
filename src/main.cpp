/**
 * ESP32 Car HUD - BLE Receiver + ST7789 Display (v3)
 *
 * Changes vs v2:
 *   1. NAV layout split 40/60 vertically:
 *        - Left 40% (0..128 px)  : large speed readout
 *        - Right 60% (128..320)  : arrow + distance + street name
 *      A 1-pixel divider separates the two zones.
 *   2. Distance formatting:
 *        - Phone may send distance either in meters (recommended) or in
 *          km. ESP32 normalizes and prints with 1 decimal place when in
 *          km range, e.g. "850 m", "1.3 km", "5.6 km".
 *        - Protocol "cách A" (recommended): app always sends
 *          { "t":"nav", "arr":"...", "d": <meters int>, "s":"..." }.
 *          The "u" field is ignored / optional. Below 1000m -> "N m",
 *          1000m and above -> "X.Y km".
 *        - Fallback "cách B": if app explicitly sends "u":"km", the "d"
 *          value is treated as a float in km. JSON parser supports both
 *          integer and float for "d".
 *   3. IDLE page: large centered speed when fresh data is available;
 *      otherwise the "Car HUD Ready" splash remains.
 *
 * Compatible with: Arduino-ESP32 core 2.x + NimBLE-Arduino 1.4.x + ArduinoJson 6.x
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

// ============================================================
//  Forward declarations
// ============================================================
enum ArrowType : int;
enum DisplayFlip : uint8_t;
static void  str_canon(const char* in, char* out, size_t outSize);
static ArrowType parseArrow(const char* raw);
static bool  parseDisplayFlip(const char* raw, DisplayFlip* out);
static const char* displayFlipToWire(DisplayFlip flip);
static void  loadDisplayConfig();
static void  saveDisplayConfig();
static void  setupBacklight();
static void  applyBacklight();
static void  applyDisplayTransform();
static void  handleConfigMessage(JsonObject doc);
static void  formatDistance(float meters, bool unitIsKm,
                            char* out, size_t outSize);
void  handleJsonMessage(const std::string& raw);
static void  drawShaft(int cx, int cy, int len, int thick, uint16_t color);
void  drawArrowShape(ArrowType type, int cx, int cy, int s, uint16_t color);
void  drawTopBar();
void  drawBottomBar();
void  drawSpeedPanel(int x, int y, int w, int h);
void  drawPageIdle();
void  drawHudPageIdle();
static bool  abbreviateStreet(char* s, size_t n);
static void  wrapTwoLines(const char* text, int maxW,
                          char* line1, size_t l1Size,
                          char* line2, size_t l2Size);
void  drawPageNav();
void  drawHudPageNav();
void  drawPageCall();
void  drawHudPageCall();
void  drawPageSms();
void  drawHudPageSms();
void  checkPageExpiration();
void  render();
void  setupBLE();
void  setup();
void  loop();

// ============================================================
//  BLE UUIDs
// ============================================================
#define SERVICE_UUID  "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHAR_RX_UUID  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHAR_TX_UUID  "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define DEVICE_NAME   "CarHUD-ESP32"

// ============================================================
//  Display & Colors
// ============================================================
TFT_eSPI tft = TFT_eSPI();

#define COLOR_BG       0x0000
#define COLOR_FG       0xFFFF
#define COLOR_ACCENT   0xFD20  // orange
#define COLOR_NAV      0x07E0  // green
#define COLOR_SPEED    0xFFE0  // yellow
#define COLOR_CALL     0x07FF  // cyan
#define COLOR_SMS      0xF81F  // magenta
#define COLOR_WARN     0xF800  // red
#define COLOR_DIM      0x630C  // gray
#define COLOR_HEADER   0x0841  // dark blue
#define COLOR_HUD_GREEN 0x07E0
#define COLOR_HUD_AMBER 0xFD20
#define COLOR_HUD_WHITE 0xFFFF

const int SCREEN_W = 320;
const int SCREEN_H = 170;

// 40/60 vertical split for the NAV page content area
const int SPLIT_X      = 128;            // 40% of 320
const int CONTENT_Y    = 18;
const int CONTENT_H    = SCREEN_H - 22 - 18;  // 130

const uint8_t DISPLAY_ROTATION = 1;
const bool DEFAULT_HUD_MODE = false;
const uint8_t DEFAULT_BRIGHTNESS = 255;

enum DisplayFlip : uint8_t {
    FLIP_NONE = 0,
    FLIP_VERTICAL,
    FLIP_HORIZONTAL,
    FLIP_ROTATE_180
};

const DisplayFlip DEFAULT_HUD_FLIP = FLIP_VERTICAL;

struct DisplayConfig {
    bool hud_mode = DEFAULT_HUD_MODE;
    DisplayFlip hud_flip = DEFAULT_HUD_FLIP;
    uint8_t brightness = DEFAULT_BRIGHTNESS;
};

DisplayConfig cfg;
Preferences prefs;

// ============================================================
//  State machine
// ============================================================
enum Page {
    PAGE_IDLE = 0,
    PAGE_NAV  = 1,
    PAGE_CALL = 2,
    PAGE_SMS  = 3
};

enum ArrowType : int {
    ARR_NONE = 0,
    ARR_STRAIGHT,
    ARR_RIGHT,
    ARR_LEFT,
    ARR_SLIGHT_RIGHT,
    ARR_SLIGHT_LEFT,
    ARR_SHARP_RIGHT,
    ARR_SHARP_LEFT,
    ARR_UTURN_LEFT,
    ARR_UTURN_RIGHT,
    ARR_ARRIVE,
    ARR_UNKNOWN
};

struct StateData {
    ArrowType nav_arrow      = ARR_NONE;
    char nav_arrow_raw[16]   = "";
    float nav_dist_m         = 0.0f;   // ALWAYS stored as meters internally
    char nav_street[64]      = "";
    unsigned long nav_until  = 0;

    int  speed_kmh           = -1;
    unsigned long speed_ts   = 0;

    char call_name[32]       = "";
    char call_phone[24]      = "";
    unsigned long call_until = 0;

    char sms_from[32]        = "";
    char sms_msg[128]        = "";
    unsigned long sms_until  = 0;

    int  clk_hour            = -1;
    int  clk_min             = -1;
    unsigned long clk_ts     = 0;

    int  bat_pct             = -1;
    unsigned long bat_ts     = 0;

    bool ble_connected       = false;
};

StateData st;
Page currentPage   = PAGE_IDLE;
Page lastDrawnPage = (Page)(-1);
bool lastDrawnHudMode = DEFAULT_HUD_MODE;
bool needsRedraw   = true;

// Track previous speed value separately so we can repaint just the
// speed panel without redrawing the whole NAV page every second.
int  lastDrawnSpeed = -2;
bool lastSpeedFresh = false;

const unsigned long NAV_TIMEOUT  = 60000;
const unsigned long CALL_TIMEOUT = 30000;
const unsigned long SMS_TIMEOUT  = 15000;
const unsigned long SPEED_STALE  = 10000;
const unsigned long CLOCK_STALE  = 120000;
const unsigned long BAT_STALE    = 120000;

NimBLECharacteristic* pTxChar = nullptr;
NimBLEServer*         pServer = nullptr;

// ============================================================
//  Display config, HUD transform, backlight
// ============================================================
static bool parseDisplayFlip(const char* raw, DisplayFlip* out) {
    if (!raw || !out) return false;

    char k[20];
    str_canon(raw, k, sizeof(k));

    if (!strcmp(k, "none") || !strcmp(k, "off") ||
        !strcmp(k, "normal") || !strcmp(k, "n")) {
        *out = FLIP_NONE;
        return true;
    }
    if (!strcmp(k, "v") || !strcmp(k, "vertical") ||
        !strcmp(k, "topbottom") || !strcmp(k, "updown") ||
        !strcmp(k, "tb") || !strcmp(k, "ud")) {
        *out = FLIP_VERTICAL;
        return true;
    }
    if (!strcmp(k, "h") || !strcmp(k, "horizontal") ||
        !strcmp(k, "leftright") || !strcmp(k, "lr")) {
        *out = FLIP_HORIZONTAL;
        return true;
    }
    if (!strcmp(k, "r180") || !strcmp(k, "rotate180") ||
        !strcmp(k, "rot180") || !strcmp(k, "180")) {
        *out = FLIP_ROTATE_180;
        return true;
    }
    return false;
}

static const char* displayFlipToWire(DisplayFlip flip) {
    switch (flip) {
        case FLIP_VERTICAL:   return "v";
        case FLIP_HORIZONTAL: return "h";
        case FLIP_ROTATE_180: return "r180";
        default:              return "none";
    }
}

static DisplayFlip sanitizeDisplayFlip(uint8_t value) {
    if (value <= (uint8_t)FLIP_ROTATE_180) return (DisplayFlip)value;
    return DEFAULT_HUD_FLIP;
}

static void loadDisplayConfig() {
    prefs.begin("display", true);
    cfg.hud_mode = prefs.getBool("hud", DEFAULT_HUD_MODE);
    cfg.hud_flip = sanitizeDisplayFlip(prefs.getUChar("flip", DEFAULT_HUD_FLIP));
    cfg.brightness = prefs.getUChar("br", DEFAULT_BRIGHTNESS);
    prefs.end();

    Serial.printf("[CFG] mode=%s flip=%s br=%u\n",
                  cfg.hud_mode ? "hud" : "normal",
                  displayFlipToWire(cfg.hud_flip),
                  cfg.brightness);
}

static void saveDisplayConfig() {
    prefs.begin("display", false);
    prefs.putBool("hud", cfg.hud_mode);
    prefs.putUChar("flip", (uint8_t)cfg.hud_flip);
    prefs.putUChar("br", cfg.brightness);
    prefs.end();
}

static void notifyConfigAck() {
    if (!pTxChar) return;

    char msg[96];
    snprintf(msg, sizeof(msg),
             "{\"t\":\"cfg\",\"mode\":\"%s\",\"flip\":\"%s\",\"br\":%u}",
             cfg.hud_mode ? "hud" : "normal",
             displayFlipToWire(cfg.hud_flip),
             cfg.brightness);

    pTxChar->setValue(msg);
    if (st.ble_connected) pTxChar->notify();
}

static uint8_t madctlForHudFlip(DisplayFlip flip) {
    // Base is TFT_eSPI ST7789 landscape rotation 1:
    // TFT_MAD_MX | TFT_MAD_MV | TFT_MAD_COLOR_ORDER.
    // The extra cases keep the logical 320x170 drawing area, but flip the
    // controller scan direction so reflection on glass can be corrected.
    switch (flip) {
        case FLIP_VERTICAL:
            return TFT_MAD_MX | TFT_MAD_MY | TFT_MAD_MV | TFT_MAD_COLOR_ORDER;
        case FLIP_HORIZONTAL:
            return TFT_MAD_MV | TFT_MAD_COLOR_ORDER;
        case FLIP_ROTATE_180:
            return TFT_MAD_MY | TFT_MAD_MV | TFT_MAD_COLOR_ORDER;
        default:
            return TFT_MAD_MX | TFT_MAD_MV | TFT_MAD_COLOR_ORDER;
    }
}

static void applyDisplayTransform() {
    DisplayFlip activeFlip = cfg.hud_mode ? cfg.hud_flip : FLIP_NONE;

    tft.setRotation(DISPLAY_ROTATION);
    tft.writecommand(TFT_MADCTL);
    tft.writedata(madctlForHudFlip(activeFlip));
}

#if defined(TFT_BL)
static const uint8_t BL_PWM_CHANNEL = 0;
static const uint32_t BL_PWM_FREQ = 5000;
static const uint8_t BL_PWM_BITS = 8;
static bool backlightReady = false;
#endif

static void applyBacklight() {
#if defined(TFT_BL)
    if (!backlightReady) return;

    uint8_t duty = cfg.brightness;
#if defined(TFT_BACKLIGHT_ON) && (TFT_BACKLIGHT_ON == LOW)
    duty = 255 - duty;
#endif
    ledcWrite(BL_PWM_CHANNEL, duty);
#endif
}

static void setupBacklight() {
#if defined(TFT_BL)
    ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ, BL_PWM_BITS);
    ledcAttachPin(TFT_BL, BL_PWM_CHANNEL);
    backlightReady = true;
    applyBacklight();
#endif
}

static void handleConfigMessage(JsonObject doc) {
    bool changed = false;
    bool transformChanged = false;
    bool brightnessChanged = false;

    bool newHudMode = cfg.hud_mode;
    DisplayFlip newFlip = cfg.hud_flip;
    uint8_t newBrightness = cfg.brightness;

    if (doc.containsKey("mode")) {
        const char* mode = doc["mode"] | "";
        char k[12];
        str_canon(mode, k, sizeof(k));

        if (!strcmp(k, "hud")) {
            newHudMode = true;
        } else if (!strcmp(k, "normal") || !strcmp(k, "direct") ||
                   !strcmp(k, "screen")) {
            newHudMode = false;
        } else {
            Serial.printf("[CFG] unknown mode: %s\n", mode);
        }
    }

    if (doc.containsKey("hud")) {
        newHudMode = doc["hud"].as<bool>();
    }

    if (doc.containsKey("flip")) {
        const char* flipRaw = doc["flip"] | "";
        if (!parseDisplayFlip(flipRaw, &newFlip)) {
            Serial.printf("[CFG] unknown flip: %s\n", flipRaw);
        }
    }

    int br = -1;
    if (doc.containsKey("br")) {
        br = doc["br"] | -1;
    } else if (doc.containsKey("brightness")) {
        br = doc["brightness"] | -1;
    }
    if (br >= 0) {
        if (br > 255) br = 255;
        newBrightness = (uint8_t)br;
    }

    if (newHudMode != cfg.hud_mode) {
        cfg.hud_mode = newHudMode;
        changed = true;
        transformChanged = true;
    }
    if (newFlip != cfg.hud_flip) {
        cfg.hud_flip = newFlip;
        changed = true;
        if (cfg.hud_mode) transformChanged = true;
    }
    if (newBrightness != cfg.brightness) {
        cfg.brightness = newBrightness;
        changed = true;
        brightnessChanged = true;
    }

    if (brightnessChanged) applyBacklight();
    if (transformChanged) {
        applyDisplayTransform();
        tft.fillScreen(COLOR_BG);
        lastDrawnPage = (Page)(-1);
    }

    bool save = doc["save"] | true;
    if (changed && save) saveDisplayConfig();

    Serial.printf("[CFG] mode=%s flip=%s br=%u%s\n",
                  cfg.hud_mode ? "hud" : "normal",
                  displayFlipToWire(cfg.hud_flip),
                  cfg.brightness,
                  save ? " saved" : "");

    needsRedraw = true;
    notifyConfigAck();
}

// ============================================================
//  Arrow string normalization
// ============================================================
static void str_canon(const char* in, char* out, size_t outSize) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < outSize; i++) {
        char c = in[i];
        if (c == '-' || c == '_' || c == ' ') continue;
        out[j++] = (char)tolower((unsigned char)c);
    }
    out[j] = '\0';
}

static ArrowType parseArrow(const char* raw) {
    if (!raw || !*raw) return ARR_NONE;

    char k[24];
    str_canon(raw, k, sizeof(k));

    if (!strcmp(k, "straight") || !strcmp(k, "up") ||
        !strcmp(k, "continue") || !strcmp(k, "forward") ||
        !strcmp(k, "depart"))                            return ARR_STRAIGHT;

    if (!strcmp(k, "right")   || !strcmp(k, "turnright") ||
        !strcmp(k, "turn0")   || !strcmp(k, "r"))         return ARR_RIGHT;

    if (!strcmp(k, "left")    || !strcmp(k, "turnleft") ||
        !strcmp(k, "l"))                                  return ARR_LEFT;

    if (!strcmp(k, "slightright")  || !strcmp(k, "bearright") ||
        !strcmp(k, "keepright"))                          return ARR_SLIGHT_RIGHT;

    if (!strcmp(k, "slightleft")   || !strcmp(k, "bearleft") ||
        !strcmp(k, "keepleft"))                           return ARR_SLIGHT_LEFT;

    if (!strcmp(k, "sharpright"))                         return ARR_SHARP_RIGHT;
    if (!strcmp(k, "sharpleft"))                          return ARR_SHARP_LEFT;

    if (!strcmp(k, "uturn")        || !strcmp(k, "uturnleft") ||
        !strcmp(k, "makeuturn"))                          return ARR_UTURN_LEFT;
    if (!strcmp(k, "uturnright"))                         return ARR_UTURN_RIGHT;

    if (!strcmp(k, "arrive")  || !strcmp(k, "arrived") ||
        !strcmp(k, "destination") || !strcmp(k, "end"))   return ARR_ARRIVE;

    return ARR_UNKNOWN;
}

// ============================================================
//  Distance formatting
//  Input: distance in meters (float). Outputs strings like:
//      "0 m", "5 m", "950 m", "1.0 km", "1.3 km", "12.4 km", "999 km"
//  Rules:
//    - meters < 1000 -> integer meters + " m"
//    - meters >= 1000 -> km with exactly 1 decimal (dot, not comma)
//    - meters < 0 or NaN -> "-- m"
// ============================================================
static void formatDistance(float meters, bool /*unitIsKm*/,
                           char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return;
    if (isnan(meters) || meters < 0.0f) {
        strlcpy(out, "-- m", outSize);
        return;
    }
    if (meters < 1000.0f) {
        int m = (int)(meters + 0.5f);
        snprintf(out, outSize, "%d m", m);
    } else {
        // round to 1 decimal in km
        float km = meters / 1000.0f;
        // avoid showing "1000.0 km" for huge values — cap visually
        if (km >= 9999.0f) km = 9999.0f;
        // snprintf with %.1f uses the C locale (dot), which is what we want.
        snprintf(out, outSize, "%.1f km", km);
    }
}

// ============================================================
//  BLE Server callbacks
// ============================================================
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pSrv, ble_gap_conn_desc* desc) override {
        st.ble_connected = true;
        needsRedraw = true;
        Serial.printf("[BLE] Connected. Conn handle: %d\n", desc->conn_handle);
        pSrv->updateConnParams(desc->conn_handle, 24, 48, 0, 60);
    }
    void onDisconnect(NimBLEServer* pSrv) override {
        st.ble_connected = false;
        needsRedraw = true;
        Serial.println("[BLE] Disconnected. Restart advertising.");
        NimBLEDevice::startAdvertising();
    }
    void onMTUChange(uint16_t MTU, ble_gap_conn_desc* desc) override {
        Serial.printf("[BLE] MTU updated to %u for conn %d\n", MTU, desc->conn_handle);
    }
};

// ============================================================
//  JSON message handler
// ============================================================
void handleJsonMessage(const std::string& raw) {
    Serial.printf("[RX] %s\n", raw.c_str());

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, raw);
    if (err) {
        Serial.printf("[JSON] parse error: %s\n", err.c_str());
        return;
    }

    const char* type = doc["t"] | "";
    unsigned long now = millis();

    if (!strcmp(type, "nav")) {
        const char* arrRaw = doc["arr"] | "";
        strlcpy(st.nav_arrow_raw, arrRaw, sizeof(st.nav_arrow_raw));
        st.nav_arrow = parseArrow(arrRaw);
        if (st.nav_arrow == ARR_UNKNOWN) {
            Serial.printf("[NAV] Unknown arrow string: '%s'\n", arrRaw);
        }

        // Distance: accept either integer meters (recommended) or
        // a float that may be in meters OR kilometers depending on "u".
        // Internally we always store meters.
        const char* unitRaw = doc["u"] | "m";
        float rawDist = doc["d"] | 0.0f;
        if (unitRaw && (unitRaw[0] == 'k' || unitRaw[0] == 'K')) {
            st.nav_dist_m = rawDist * 1000.0f;
        } else {
            st.nav_dist_m = rawDist;
        }

        strlcpy(st.nav_street, doc["s"]  | "",  sizeof(st.nav_street));
        st.nav_until = now + NAV_TIMEOUT;
        currentPage = PAGE_NAV;
        needsRedraw = true;
    }
    else if (!strcmp(type, "spd")) {
        st.speed_kmh = doc["v"] | 0;
        st.speed_ts  = now;
        needsRedraw  = true;
    }
    else if (!strcmp(type, "call")) {
        strlcpy(st.call_name,  doc["n"] | "Unknown", sizeof(st.call_name));
        strlcpy(st.call_phone, doc["p"] | "",        sizeof(st.call_phone));
        st.call_until = now + CALL_TIMEOUT;
        currentPage = PAGE_CALL;
        needsRedraw = true;
    }
    else if (!strcmp(type, "sms")) {
        strlcpy(st.sms_from, doc["f"] | "Unknown", sizeof(st.sms_from));
        strlcpy(st.sms_msg,  doc["m"] | "",        sizeof(st.sms_msg));
        st.sms_until = now + SMS_TIMEOUT;
        currentPage = PAGE_SMS;
        needsRedraw = true;
    }
    else if (!strcmp(type, "clk")) {
        st.clk_hour = doc["h"] | -1;
        st.clk_min  = doc["m"] | -1;
        st.clk_ts   = now;
        needsRedraw = true;
    }
    else if (!strcmp(type, "bat")) {
        st.bat_pct = doc["p"] | -1;
        st.bat_ts  = now;
        needsRedraw = true;
    }
    else if (!strcmp(type, "cfg") || !strcmp(type, "hud")) {
        handleConfigMessage(doc.as<JsonObject>());
    }
    else if (!strcmp(type, "clr")) {
        currentPage = PAGE_IDLE;
        st.nav_until = 0;
        st.call_until = 0;
        st.sms_until = 0;
        needsRedraw = true;
    }
    else {
        Serial.printf("[JSON] unknown type: %s\n", type);
    }
}

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (!value.empty()) handleJsonMessage(value);
    }
};

// ============================================================
//  Arrow drawing — unchanged from v2
// ============================================================
static void drawShaft(int cx, int cy, int len, int thick, uint16_t color) {
    tft.fillRect(cx - thick / 2, cy - len / 2, thick, len, color);
}

void drawArrowShape(ArrowType type, int cx, int cy, int s, uint16_t color) {
    switch (type) {
        case ARR_RIGHT: {
            tft.fillRect(cx - s, cy - s / 4, s + s / 3, s / 2, color);
            tft.fillTriangle(cx + s / 3, cy - s,
                             cx + s / 3, cy + s,
                             cx + s,     cy, color);
            break;
        }
        case ARR_LEFT: {
            tft.fillRect(cx - s / 3, cy - s / 4, s + s / 3, s / 2, color);
            tft.fillTriangle(cx - s / 3, cy - s,
                             cx - s / 3, cy + s,
                             cx - s,     cy, color);
            break;
        }
        case ARR_STRAIGHT: {
            tft.fillRect(cx - s / 4, cy - s / 2, s / 2, s + s / 3, color);
            tft.fillTriangle(cx - s, cy - s / 2,
                             cx + s, cy - s / 2,
                             cx,     cy - s, color);
            break;
        }
        case ARR_SLIGHT_RIGHT: {
            tft.fillTriangle(cx - s / 2, cy + s,
                             cx + s,     cy - s / 2,
                             cx + s / 2, cy - s / 4 + s / 4, color);
            tft.fillTriangle(cx + s,     cy - s,
                             cx + s / 4, cy - s / 2,
                             cx + s,     cy - s / 4, color);
            break;
        }
        case ARR_SLIGHT_LEFT: {
            tft.fillTriangle(cx + s / 2, cy + s,
                             cx - s,     cy - s / 2,
                             cx - s / 2, cy - s / 4 + s / 4, color);
            tft.fillTriangle(cx - s,     cy - s,
                             cx - s / 4, cy - s / 2,
                             cx - s,     cy - s / 4, color);
            break;
        }
        case ARR_SHARP_RIGHT: {
            tft.fillRect(cx - s / 4, cy - s / 8, s / 2, s + s / 8, color);
            tft.fillRect(cx - s / 4, cy - s / 8, s, s / 2, color);
            tft.fillTriangle(cx + s - s / 8, cy - s / 2,
                             cx + s - s / 8, cy + s / 2,
                             cx + s + s / 3, cy + s / 8, color);
            break;
        }
        case ARR_SHARP_LEFT: {
            tft.fillRect(cx - s / 4, cy - s / 8, s / 2, s + s / 8, color);
            tft.fillRect(cx - s,     cy - s / 8, s + s / 4, s / 2, color);
            tft.fillTriangle(cx - s + s / 8, cy - s / 2,
                             cx - s + s / 8, cy + s / 2,
                             cx - s - s / 3, cy + s / 8, color);
            break;
        }
        case ARR_UTURN_LEFT: {
            tft.fillRect(cx + s / 3,        cy - s / 4, s / 3, s + s / 4, color);
            tft.fillRect(cx - s / 2,        cy - s,     s + s / 6, s / 3, color);
            tft.fillRect(cx - s / 2,        cy - s,     s / 3, s / 2 + s / 8, color);
            tft.fillTriangle(cx - s / 2 - s / 3, cy - s / 8,
                             cx - s / 2 + s / 3, cy - s / 8,
                             cx - s / 6,         cy + s / 2, color);
            break;
        }
        case ARR_UTURN_RIGHT: {
            tft.fillRect(cx - s / 3 - s / 3, cy - s / 4, s / 3, s + s / 4, color);
            tft.fillRect(cx - s / 3,         cy - s,     s + s / 6, s / 3, color);
            tft.fillRect(cx + s / 6,         cy - s,     s / 3, s / 2 + s / 8, color);
            tft.fillTriangle(cx + s / 6 - s / 3, cy - s / 8,
                             cx + s / 6 + s / 3, cy - s / 8,
                             cx + s / 6 + s / 6, cy + s / 2, color);
            break;
        }
        case ARR_ARRIVE: {
            drawShaft(cx, cy + s / 4, s + s / 2, s / 6, color);
            tft.fillRect(cx, cy - s, s, s / 2 + s / 4, color);
            int sq = s / 5;
            tft.fillRect(cx + sq,     cy - s + sq, sq, sq, COLOR_BG);
            tft.fillRect(cx + 3 * sq, cy - s + sq, sq, sq, COLOR_BG);
            tft.fillRect(cx,          cy - s,      sq, sq, COLOR_BG);
            tft.fillRect(cx + 2 * sq, cy - s,      sq, sq, COLOR_BG);
            break;
        }
        default: {
            tft.fillCircle(cx, cy, s, COLOR_WARN);
            tft.setTextColor(COLOR_FG, COLOR_WARN);
            tft.setTextFont(4);
            tft.setCursor(cx - 6, cy - 12);
            tft.print("?");
            break;
        }
    }
}

// ============================================================
//  Top / bottom bars
// ============================================================
void drawTopBar() {
    tft.fillRect(0, 0, SCREEN_W, 18, COLOR_HEADER);
    tft.setTextColor(COLOR_FG, COLOR_HEADER);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setCursor(6, 4);
    tft.print(st.ble_connected ? "BLE OK" : "BLE...");

    const char* tag = "READY";
    switch (currentPage) {
        case PAGE_NAV:  tag = "NAVIGATION";    break;
        case PAGE_CALL: tag = "INCOMING CALL"; break;
        case PAGE_SMS:  tag = "MESSAGE";       break;
        default:        tag = "READY";         break;
    }
    int tagW = strlen(tag) * 6;
    tft.setCursor(SCREEN_W - tagW - 6, 4);
    tft.print(tag);
}

void drawBottomBar() {
    const int barY = SCREEN_H - 22;
    tft.fillRect(0, barY, SCREEN_W, 22, COLOR_BG);
    tft.drawFastHLine(0, barY, SCREEN_W, COLOR_DIM);

    unsigned long now = millis();
    char buf[16];

    // On NAV page the speed is already shown big on the left panel,
    // so the bottom bar shows only clock + battery (cleaner).
    tft.setTextFont(2);

    if (currentPage != PAGE_NAV) {
        // speed (left)
        if (st.speed_kmh >= 0 && (now - st.speed_ts < SPEED_STALE)) {
            tft.setTextColor(COLOR_SPEED, COLOR_BG);
            snprintf(buf, sizeof(buf), "%d km/h", st.speed_kmh);
        } else {
            tft.setTextColor(COLOR_DIM, COLOR_BG);
            snprintf(buf, sizeof(buf), "-- km/h");
        }
        tft.setCursor(8, barY + 3);
        tft.print(buf);
    }

    // clock (centered)
    if (st.clk_hour >= 0 && (now - st.clk_ts < CLOCK_STALE)) {
        tft.setTextColor(COLOR_FG, COLOR_BG);
        snprintf(buf, sizeof(buf), "%02d:%02d", st.clk_hour, st.clk_min);
    } else {
        tft.setTextColor(COLOR_DIM, COLOR_BG);
        snprintf(buf, sizeof(buf), "--:--");
    }
    int clkW = tft.textWidth(buf);
    tft.setCursor((SCREEN_W - clkW) / 2, barY + 3);
    tft.print(buf);

    // battery (right)
    if (st.bat_pct >= 0 && (now - st.bat_ts < BAT_STALE)) {
        if (st.bat_pct <= 20) tft.setTextColor(COLOR_WARN, COLOR_BG);
        else                  tft.setTextColor(COLOR_FG,   COLOR_BG);
        snprintf(buf, sizeof(buf), "Bat %d%%", st.bat_pct);
    } else {
        tft.setTextColor(COLOR_DIM, COLOR_BG);
        snprintf(buf, sizeof(buf), "Bat --");
    }
    int batW = tft.textWidth(buf);
    tft.setCursor(SCREEN_W - batW - 8, barY + 3);
    tft.print(buf);
}

// ============================================================
//  Speed panel (used on the NAV page, left 40%)
//  Draws inside the given rectangle. Repaints only this area.
//
//  Font sizing:
//    - Number: font 7 (7-seg, ~48px) at setTextSize(2) -> ~96px tall.
//      This is the biggest the digits can be without exceeding the
//      128 px wide / 130 px tall panel. For 3-digit speeds (e.g. 120)
//      the width at size 2 is roughly 3 * 30px*2 = ~180px which would
//      NOT fit, so we auto-fall-back to size 1 when there are 3+ digits.
//    - Unit "km/h": font 4 (~26px) instead of font 2 so it scales up
//      with the number.
// ============================================================
void drawSpeedPanel(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, COLOR_BG);

    unsigned long now = millis();
    bool fresh = (st.speed_kmh >= 0) && (now - st.speed_ts < SPEED_STALE);

    char numBuf[8];
    if (fresh) {
        snprintf(numBuf, sizeof(numBuf), "%d", st.speed_kmh);
        tft.setTextColor(COLOR_SPEED, COLOR_BG);
    } else {
        strlcpy(numBuf, "--", sizeof(numBuf));
        tft.setTextColor(COLOR_DIM, COLOR_BG);
    }

    // Try size 2 first (biggest). If too wide for the panel, fall back
    // to size 1. Reserve a small horizontal margin so digits don't kiss
    // the divider line.
    const int hMargin = 4;
    tft.setTextFont(7);
    tft.setTextSize(2);
    int numW = tft.textWidth(numBuf);
    int numH = 96;  // font 7 (~48px) * size 2
    int unitFont = 4;
    int unitGap  = 6;

    if (numW > w - hMargin * 2) {
        // Fallback for 3-digit speeds: size 1 (still uses font 7)
        tft.setTextSize(1);
        numW = tft.textWidth(numBuf);
        numH = 48;
        unitFont = 4;
        unitGap  = 4;
    }

    // Unit height contributes to vertical centering
    tft.setTextFont(unitFont);
    int unitH = tft.fontHeight();
    int blockH = numH + unitGap + unitH;
    // +2 px nudge so the block sits slightly below true center,
    // which looks better visually against the divider and bottom bar.
    int blockY = y + (h - blockH) / 2 + 4;

    // Draw number
    tft.setTextFont(7);
    int numX = x + (w - numW) / 2;
    int numY = blockY;
    tft.setCursor(numX, numY);
    tft.print(numBuf);

    // Restore size 1 for non-speed drawing elsewhere
    tft.setTextSize(1);

    // Draw unit label below
    tft.setTextFont(unitFont);
    tft.setTextColor(fresh ? COLOR_ACCENT : COLOR_DIM, COLOR_BG);
    const char* unit = "km/h";
    int uW = tft.textWidth(unit);
    tft.setCursor(x + (w - uW) / 2, numY + numH + unitGap);
    tft.print(unit);

    lastDrawnSpeed = st.speed_kmh;
    lastSpeedFresh = fresh;
}

// ============================================================
//  IDLE page — speed big in center when available
// ============================================================
void drawPageIdle() {
    tft.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COLOR_BG);

    unsigned long now = millis();
    bool fresh = (st.speed_kmh >= 0) && (now - st.speed_ts < SPEED_STALE);

    if (fresh) {
        // Huge centered speed
        char numBuf[8];
        snprintf(numBuf, sizeof(numBuf), "%d", st.speed_kmh);
        tft.setTextFont(7);
        tft.setTextColor(COLOR_SPEED, COLOR_BG);
        int numW = tft.textWidth(numBuf);
        tft.setCursor((SCREEN_W - numW) / 2, CONTENT_Y + 20);
        tft.print(numBuf);

        tft.setTextFont(4);
        tft.setTextColor(COLOR_ACCENT, COLOR_BG);
        const char* unit = "km/h";
        int uW = tft.textWidth(unit);
        tft.setCursor((SCREEN_W - uW) / 2, CONTENT_Y + 85);
        tft.print(unit);
    } else {
        tft.setTextColor(COLOR_DIM, COLOR_BG);
        tft.setTextFont(4);
        const char* msg1 = "Car HUD Ready";
        int w1 = tft.textWidth(msg1);
        tft.setCursor((SCREEN_W - w1) / 2, CONTENT_Y + 25);
        tft.print(msg1);

        tft.setTextFont(2);
        const char* msg2 = st.ble_connected
                             ? "Waiting for data..."
                             : "Connect from your phone";
        int w2 = tft.textWidth(msg2);
        tft.setCursor((SCREEN_W - w2) / 2, CONTENT_Y + 70);
        tft.print(msg2);
    }

    lastDrawnSpeed = st.speed_kmh;
    lastSpeedFresh = fresh;
}

// ============================================================
//  Street-name helpers — unchanged from v2
// ============================================================
static bool abbreviateStreet(char* s, size_t n) {
    struct Sub { const char* from; const char* to; };
    static const Sub subs[] = {
        { "Den noi luc",   "EAT"   },
        { "den noi luc",   "EAT"   },
        { "Re phai vao",   "R:"    },
        { "Re trai vao",   "L:"    },
        { "Re phai",       "R"     },
        { "Re trai",       "L"     },
        { "Di thang",      "Fwd"   },
        { "Quay dau",      "U-turn"},
        { "Duong",         "D."    },
        { "Pho",           "P."    },
        { "Quan",          "Q."    },
        { "Continue on",   "Cont." },
        { "Turn right onto", "R:"  },
        { "Turn left onto",  "L:"  },
        { "Turn right",    "R"     },
        { "Turn left",     "L"     },
        { "Street",        "St"    },
        { "Avenue",        "Ave"   },
        { "Boulevard",     "Blvd"  },
        { "Road",          "Rd"    },
    };
    bool changed = false;
    for (size_t i = 0; i < sizeof(subs)/sizeof(subs[0]); i++) {
        const char* needle = subs[i].from;
        size_t nlen = strlen(needle);
        size_t rlen = strlen(subs[i].to);
        char* p = strstr(s, needle);
        while (p) {
            size_t tail = strlen(p + nlen);
            if ((p - s) + rlen + tail + 1 > n) break;
            memmove(p + rlen, p + nlen, tail + 1);
            memcpy(p, subs[i].to, rlen);
            changed = true;
            p = strstr(p + rlen, needle);
        }
    }
    return changed;
}

static void wrapTwoLines(const char* text, int maxW,
                         char* line1, size_t l1Size,
                         char* line2, size_t l2Size) {
    line1[0] = '\0';
    line2[0] = '\0';
    if (!text || !*text) return;

    char buf[160];
    strlcpy(buf, text, sizeof(buf));

    int len = strlen(buf);
    int cut = len;
    while (cut > 0) {
        char saved = buf[cut];
        buf[cut] = '\0';
        if (tft.textWidth(buf) <= maxW) {
            buf[cut] = saved;
            break;
        }
        buf[cut] = saved;
        cut--;
    }
    if (cut < len) {
        int back = cut;
        while (back > 1 && buf[back] != ' ') back--;
        if (back > len / 2) cut = back;
    }
    strlcpy(line1, buf, l1Size);
    line1[cut < (int)l1Size ? cut : (int)l1Size - 1] = '\0';

    if (cut >= len) return;

    const char* rest = text + cut;
    while (*rest == ' ') rest++;
    strlcpy(line2, rest, l2Size);

    while (tft.textWidth(line2) > maxW && strlen(line2) > 2) {
        size_t L = strlen(line2);
        line2[L - 1] = '\0';
        if (L >= 3) {
            line2[L - 3] = '.';
            line2[L - 2] = '.';
            line2[L - 1] = '\0';
        }
    }
}

// ============================================================
//  NAV page — 40/60 split
//
//  Layout:
//    Left zone  (x: 0..127, w=128) : speed panel (drawSpeedPanel)
//    Divider    (x: 128, 1 px)     : vertical line in COLOR_DIM
//    Right zone (x: 129..319, w=191) : arrow + distance + street
//
//  Right zone breakdown (Y inside CONTENT area, height 130):
//    Arrow:    cx = 129 + 30 = 159, cy = CONTENT_Y + 32, size = 26
//    Distance: x = 129 + 64, y = CONTENT_Y + 8  (font 6 number + font 4 unit)
//    Street:   x = 129 + 8,  y = CONTENT_Y + 78 (font 2, up to 2 lines)
// ============================================================
void drawPageNav() {
    // Wipe content area
    tft.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COLOR_BG);

    // Vertical divider between speed (left) and nav (right)
    tft.drawFastVLine(SPLIT_X, CONTENT_Y, CONTENT_H, COLOR_DIM);

    // ---- Left: speed panel ----
    drawSpeedPanel(0, CONTENT_Y, SPLIT_X, CONTENT_H);

    // ---- Right: arrow ----
    const int rightX  = SPLIT_X + 1;
    const int rightW  = SCREEN_W - rightX;             // 191
    const int arrowCX = rightX + 32;
    const int arrowCY = CONTENT_Y + 36;
    const int arrowS  = 26;

    uint16_t arrowColor = (st.nav_arrow == ARR_UNKNOWN || st.nav_arrow == ARR_NONE)
                           ? COLOR_WARN : COLOR_NAV;
    drawArrowShape(st.nav_arrow, arrowCX, arrowCY, arrowS, arrowColor);

    // ---- Right: distance + unit ----
    char distStr[16];
    formatDistance(st.nav_dist_m, false, distStr, sizeof(distStr));

    // Split into <number> and <unit> so we can render at two sizes.
    // distStr looks like "850 m" or "1.3 km" — split at the space.
    char distNum[12] = "";
    char distUnit[6] = "";
    {
        const char* sp = strchr(distStr, ' ');
        if (sp) {
            size_t nlen = (size_t)(sp - distStr);
            if (nlen >= sizeof(distNum)) nlen = sizeof(distNum) - 1;
            memcpy(distNum, distStr, nlen);
            distNum[nlen] = '\0';
            strlcpy(distUnit, sp + 1, sizeof(distUnit));
        } else {
            strlcpy(distNum, distStr, sizeof(distNum));
        }
    }

    const int distAreaX = rightX + 64;  // to the right of the arrow
    tft.setTextColor(COLOR_FG, COLOR_BG);

    // Pick font for number: font 6 is digits + ':' + '.' only (perfect
    // for "1.3"). Verify width fits in the right zone.
    tft.setTextFont(6);
    int numW = tft.textWidth(distNum);
    int numY = CONTENT_Y + 8;

    // If width is borderline (long km values like "999.9"), drop down a
    // size to font 4 instead so it never clips the screen edge.
    int unitGap = 6;
    int needW = numW + unitGap + 30;  // approx unit width with font 4
    if (rightX + (distAreaX - rightX) + needW > SCREEN_W - 4) {
        tft.setTextFont(4);
        numW = tft.textWidth(distNum);
        numY = CONTENT_Y + 16;
    }
    tft.setCursor(distAreaX, numY);
    tft.print(distNum);

    // Unit text — baseline-align with the bottom of the big number
    tft.setTextFont(4);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    int unitY = numY + 22;  // ~baseline offset from font 6 top
    tft.setCursor(distAreaX + numW + unitGap, unitY);
    tft.print(distUnit);

    // ---- Right: street name (2 lines max) ----
    //
    // If the text looks like an ETA line (contains "Den noi luc", "den
    // noi luc", "EAT", or "ETA"), render it in a bigger font (font 4,
    // ~26px) and as a single line, since ETA strings are short like
    // "EAT 09:35" or "Den noi luc 09:35". Otherwise use the normal
    // font 2 two-line layout for street names.
    //
    // The text is also shifted down ~2px (offset 80 instead of 78) per
    // the user request.
    int streetX    = rightX + 8;
    int streetMaxW = SCREEN_W - streetX - 6;  // available width

    char working[96];
    strlcpy(working, st.nav_street, sizeof(working));

    // Case-insensitive search for any ETA marker
    auto containsCI = [](const char* hay, const char* needle) -> bool {
        if (!hay || !needle || !*needle) return false;
        size_t nlen = strlen(needle);
        for (const char* p = hay; *p; p++) {
            size_t i = 0;
            for (; i < nlen; i++) {
                if (tolower((unsigned char)p[i]) !=
                    tolower((unsigned char)needle[i])) break;
            }
            if (i == nlen) return true;
        }
        return false;
    };

    bool isEta = containsCI(working, "den noi luc") ||
                 containsCI(working, "EAT") ||
                 containsCI(working, "ETA");

    int streetY = CONTENT_Y + 80;   // shifted down 2px (was 78)

    if (isEta) {
        // Bigger font, single line, with abbreviation + truncation fallback
        tft.setTextFont(4);
        tft.setTextColor(COLOR_ACCENT, COLOR_BG);

        // Apply abbreviation table first so "Den noi luc 09:35" becomes
        // "EAT 09:35" and fits comfortably.
        abbreviateStreet(working, sizeof(working));

        // Truncate if still too wide
        while (tft.textWidth(working) > streetMaxW && strlen(working) > 2) {
            size_t L = strlen(working);
            working[L - 1] = '\0';
            if (L >= 3) {
                working[L - 3] = '.';
                working[L - 2] = '.';
                working[L - 1] = '\0';
            }
        }
        tft.setCursor(streetX, streetY);
        tft.print(working);
    } else {
        // Normal street name: font 2, up to 2 lines, with abbreviation
        // fallback if overflowed.
        tft.setTextFont(2);
        tft.setTextColor(COLOR_ACCENT, COLOR_BG);

        char l1[64], l2[64];
        wrapTwoLines(working, streetMaxW, l1, sizeof(l1), l2, sizeof(l2));

        bool overflowed = (strstr(l2, "..") != nullptr);
        if (overflowed && abbreviateStreet(working, sizeof(working))) {
            wrapTwoLines(working, streetMaxW, l1, sizeof(l1), l2, sizeof(l2));
        }

        tft.setCursor(streetX, streetY);
        tft.print(l1);
        if (l2[0]) {
            tft.setCursor(streetX, streetY + 18);
            tft.print(l2);
        }
    }
}

// ============================================================
//  HUD pages
//
//  HUD mode removes top/bottom bars and keeps the driving surface
//  minimal: speed + arrow + distance. The display transform is applied
//  at controller level, so drawing coordinates stay normal here.
// ============================================================
static bool speedFreshNow() {
    unsigned long now = millis();
    return (st.speed_kmh >= 0) && (now - st.speed_ts < SPEED_STALE);
}

static void drawHudSpeedPanel(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, COLOR_BG);

    bool fresh = speedFreshNow();
    char numBuf[8];
    if (fresh) snprintf(numBuf, sizeof(numBuf), "%d", st.speed_kmh);
    else       strlcpy(numBuf, "--", sizeof(numBuf));

    tft.setTextFont(7);
    tft.setTextSize(2);
    tft.setTextColor(fresh ? COLOR_HUD_GREEN : COLOR_DIM, COLOR_BG);

    int numW = tft.textWidth(numBuf);
    int numH = 96;
    if (numW > w - 8) {
        tft.setTextSize(1);
        numW = tft.textWidth(numBuf);
        numH = 48;
    }

    tft.setTextFont(4);
    tft.setTextSize(1);
    int unitH = tft.fontHeight();
    const int unitGap = 4;
    int blockH = numH + unitGap + unitH;
    int numX = x + (w - numW) / 2;
    int numY = y + (h - blockH) / 2 + 2;

    tft.setTextFont(7);
    tft.setCursor(numX, numY);
    tft.print(numBuf);
    tft.setTextSize(1);

    tft.setTextFont(4);
    tft.setTextColor(fresh ? COLOR_HUD_AMBER : COLOR_DIM, COLOR_BG);
    const char* unit = "km/h";
    int unitW = tft.textWidth(unit);
    tft.setCursor(x + (w - unitW) / 2, numY + numH + unitGap);
    tft.print(unit);

    lastDrawnSpeed = st.speed_kmh;
    lastSpeedFresh = fresh;
}

static void drawHudMiniSpeed(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, COLOR_BG);

    bool fresh = speedFreshNow();
    char buf[8];
    if (fresh) snprintf(buf, sizeof(buf), "%d", st.speed_kmh);
    else       strlcpy(buf, "--", sizeof(buf));

    tft.setTextFont(7);
    tft.setTextSize(1);
    tft.setTextColor(fresh ? COLOR_HUD_GREEN : COLOR_DIM, COLOR_BG);
    int numW = tft.textWidth(buf);
    tft.setCursor(x + (w - numW) / 2, y + 2);
    tft.print(buf);
    tft.setTextSize(1);

    tft.setTextFont(2);
    tft.setTextColor(fresh ? COLOR_HUD_AMBER : COLOR_DIM, COLOR_BG);
    const char* unit = "km/h";
    int unitW = tft.textWidth(unit);
    tft.setCursor(x + (w - unitW) / 2, y + 52);
    tft.print(unit);

    lastDrawnSpeed = st.speed_kmh;
    lastSpeedFresh = fresh;
}

static void drawHudDistancePanel(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, COLOR_BG);

    char distStr[16];
    formatDistance(st.nav_dist_m, false, distStr, sizeof(distStr));

    char distNum[12] = "";
    char distUnit[6] = "";
    const char* sp = strchr(distStr, ' ');
    if (sp) {
        size_t nlen = (size_t)(sp - distStr);
        if (nlen >= sizeof(distNum)) nlen = sizeof(distNum) - 1;
        memcpy(distNum, distStr, nlen);
        distNum[nlen] = '\0';
        strlcpy(distUnit, sp + 1, sizeof(distUnit));
    } else {
        strlcpy(distNum, distStr, sizeof(distNum));
    }

    tft.setTextFont(6);
    tft.setTextSize(1);
    int numW = tft.textWidth(distNum);
    int numH = tft.fontHeight();
    if (numW > w - 4) {
        tft.setTextFont(4);
        numW = tft.textWidth(distNum);
        numH = tft.fontHeight();
    }

    int numX = x + (w - numW) / 2;
    int numY = y + (h - numH - 28) / 2;
    if (numY < y) numY = y;

    tft.setTextColor(COLOR_HUD_WHITE, COLOR_BG);
    tft.setCursor(numX, numY);
    tft.print(distNum);

    tft.setTextFont(4);
    tft.setTextColor(COLOR_HUD_AMBER, COLOR_BG);
    int unitW = tft.textWidth(distUnit);
    tft.setCursor(x + (w - unitW) / 2, numY + numH + 4);
    tft.print(distUnit);
}

void drawHudPageNav() {
    tft.fillScreen(COLOR_BG);

    const int speedW = 128;
    const int dividerX = speedW + 2;
    drawHudSpeedPanel(0, 0, speedW, SCREEN_H);
    tft.drawFastVLine(dividerX, 12, SCREEN_H - 24, COLOR_HUD_AMBER);

    uint16_t arrowColor = (st.nav_arrow == ARR_UNKNOWN || st.nav_arrow == ARR_NONE)
                           ? COLOR_HUD_AMBER : COLOR_HUD_GREEN;
    drawArrowShape(st.nav_arrow, 174, 82, 36, arrowColor);
    drawHudDistancePanel(220, 28, 96, 112);
}

void drawHudPageIdle() {
    tft.fillScreen(COLOR_BG);

    if (speedFreshNow()) {
        drawHudSpeedPanel(34, 0, 252, SCREEN_H);
    } else {
        drawHudSpeedPanel(70, 0, 180, SCREEN_H);
        tft.setTextFont(2);
        tft.setTextColor(COLOR_DIM, COLOR_BG);
        const char* msg = st.ble_connected ? "Waiting for data" : "BLE disconnected";
        int msgW = tft.textWidth(msg);
        tft.setCursor((SCREEN_W - msgW) / 2, SCREEN_H - 24);
        tft.print(msg);
    }
}

void drawHudPageCall() {
    tft.fillScreen(COLOR_BG);

    int icx = 42, icy = 55;
    tft.drawCircle(icx, icy, 25, COLOR_HUD_GREEN);
    tft.drawCircle(icx, icy, 26, COLOR_HUD_GREEN);
    tft.fillRoundRect(icx - 13, icy - 4, 26, 12, 4, COLOR_HUD_GREEN);
    tft.fillRect(icx - 15, icy - 13, 7, 11, COLOR_HUD_GREEN);
    tft.fillRect(icx + 8,  icy - 13, 7, 11, COLOR_HUD_GREEN);

    tft.setTextFont(2);
    tft.setTextColor(COLOR_HUD_AMBER, COLOR_BG);
    tft.setCursor(86, 16);
    tft.print("CALL");

    char nameBuf[32];
    strlcpy(nameBuf, st.call_name, sizeof(nameBuf));
    tft.setTextFont(4);
    tft.setTextColor(COLOR_HUD_WHITE, COLOR_BG);
    while (tft.textWidth(nameBuf) > 152 && strlen(nameBuf) > 3) {
        nameBuf[strlen(nameBuf) - 1] = '\0';
    }
    tft.setCursor(86, 48);
    tft.print(nameBuf);

    tft.setTextFont(2);
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setCursor(86, 88);
    tft.print(st.call_phone);

    drawHudMiniSpeed(248, 94, 68, 72);
}

void drawHudPageSms() {
    tft.fillScreen(COLOR_BG);

    tft.setTextFont(2);
    tft.setTextColor(COLOR_HUD_AMBER, COLOR_BG);
    tft.setCursor(8, 12);
    tft.print("MSG");

    char fromBuf[32];
    strlcpy(fromBuf, st.sms_from, sizeof(fromBuf));
    tft.setTextFont(4);
    tft.setTextColor(COLOR_HUD_WHITE, COLOR_BG);
    while (tft.textWidth(fromBuf) > 230 && strlen(fromBuf) > 3) {
        fromBuf[strlen(fromBuf) - 1] = '\0';
    }
    tft.setCursor(8, 38);
    tft.print(fromBuf);

    tft.setTextFont(2);
    tft.setTextColor(COLOR_HUD_GREEN, COLOR_BG);
    char l1[96], l2[96];
    wrapTwoLines(st.sms_msg, 230, l1, sizeof(l1), l2, sizeof(l2));
    tft.setCursor(8, 82);
    tft.print(l1);
    if (l2[0]) {
        tft.setCursor(8, 102);
        tft.print(l2);
    }

    drawHudMiniSpeed(248, 94, 68, 72);
}

void drawPageCall() {
    tft.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COLOR_BG);

    int icx = 50, icy = CONTENT_Y + 50;
    tft.fillCircle(icx, icy, 26, COLOR_CALL);
    tft.fillCircle(icx, icy, 20, COLOR_BG);
    tft.fillRoundRect(icx - 12, icy - 4, 24, 12, 4, COLOR_CALL);
    tft.fillRect(icx - 14, icy - 12, 6, 10, COLOR_CALL);
    tft.fillRect(icx + 8,  icy - 12, 6, 10, COLOR_CALL);

    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextFont(4);
    tft.setCursor(100, CONTENT_Y + 18);
    char nameBuf[32];
    strlcpy(nameBuf, st.call_name, sizeof(nameBuf));
    while (tft.textWidth(nameBuf) > (SCREEN_W - 100 - 8) && strlen(nameBuf) > 3) {
        nameBuf[strlen(nameBuf) - 1] = '\0';
    }
    tft.print(nameBuf);

    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextFont(2);
    tft.setCursor(100, CONTENT_Y + 60);
    tft.print(st.call_phone);

    tft.setTextColor(COLOR_CALL, COLOR_BG);
    tft.setCursor(100, CONTENT_Y + 90);
    tft.print("Incoming call");
}

void drawPageSms() {
    tft.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COLOR_BG);

    tft.fillRect(0, CONTENT_Y, SCREEN_W, 24, COLOR_SMS);
    tft.setTextColor(COLOR_BG, COLOR_SMS);
    tft.setTextFont(2);
    char fromBuf[40];
    snprintf(fromBuf, sizeof(fromBuf), "  Msg from %s", st.sms_from);
    tft.setCursor(4, CONTENT_Y + 5);
    tft.print(fromBuf);

    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextFont(2);
    const int bodyY = CONTENT_Y + 32;
    const int lineH = 18;
    const int maxLines = 4;
    const int rightMargin = SCREEN_W - 10;

    char remaining[128];
    strlcpy(remaining, st.sms_msg, sizeof(remaining));

    for (int line = 0; line < maxLines && strlen(remaining) > 0; line++) {
        char lineBuf[128];
        strlcpy(lineBuf, remaining, sizeof(lineBuf));

        while (tft.textWidth(lineBuf) > (rightMargin - 8) && strlen(lineBuf) > 1) {
            lineBuf[strlen(lineBuf) - 1] = '\0';
        }
        if (line < maxLines - 1 && strlen(lineBuf) < strlen(remaining)) {
            int len = strlen(lineBuf);
            for (int i = len - 1; i > len / 2; i--) {
                if (lineBuf[i] == ' ') { lineBuf[i] = '\0'; break; }
            }
        }
        tft.setCursor(8, bodyY + line * lineH);
        tft.print(lineBuf);

        size_t consumed = strlen(lineBuf);
        while (remaining[consumed] == ' ') consumed++;
        memmove(remaining, remaining + consumed, strlen(remaining) - consumed + 1);
    }
}

void checkPageExpiration() {
    unsigned long now = millis();
    Page next = currentPage;

    if (currentPage == PAGE_SMS && st.sms_until && now > st.sms_until) {
        st.sms_until = 0;
        next = (st.nav_until && now < st.nav_until) ? PAGE_NAV : PAGE_IDLE;
    }
    if (currentPage == PAGE_CALL && st.call_until && now > st.call_until) {
        st.call_until = 0;
        next = (st.nav_until && now < st.nav_until) ? PAGE_NAV : PAGE_IDLE;
    }
    if (currentPage == PAGE_NAV && st.nav_until && now > st.nav_until) {
        st.nav_until = 0;
        next = PAGE_IDLE;
    }

    if (next != currentPage) {
        currentPage = next;
        needsRedraw = true;
    }
}

void render() {
    // Detect speed-only updates so we don't have to repaint the whole
    // NAV page (which would visibly flicker street text every second).
    unsigned long now = millis();
    bool speedFresh = (st.speed_kmh >= 0) && (now - st.speed_ts < SPEED_STALE);
    bool speedChanged = (st.speed_kmh != lastDrawnSpeed) ||
                        (speedFresh != lastSpeedFresh);
    bool modeChanged = (cfg.hud_mode != lastDrawnHudMode);

    if (!needsRedraw && currentPage == lastDrawnPage && !modeChanged) {
        // Lightweight refresh path: only update the speed panel/area
        // when the value has changed.
        if (speedChanged) {
            if (cfg.hud_mode) {
                switch (currentPage) {
                    case PAGE_NAV:  drawHudPageNav();  break;
                    case PAGE_CALL: drawHudPageCall(); break;
                    case PAGE_SMS:  drawHudPageSms();  break;
                    default:        drawHudPageIdle(); break;
                }
            } else {
                if (currentPage == PAGE_NAV) {
                    drawSpeedPanel(0, CONTENT_Y, SPLIT_X, CONTENT_H);
                } else if (currentPage == PAGE_IDLE) {
                    drawPageIdle();
                } else {
                    // CALL/SMS show speed in the bottom bar; redraw it
                    drawBottomBar();
                    lastDrawnSpeed = st.speed_kmh;
                    lastSpeedFresh = speedFresh;
                }
            }
        }
        return;
    }

    if (currentPage != lastDrawnPage || modeChanged) {
        tft.fillScreen(COLOR_BG);
        lastDrawnPage = currentPage;
        lastDrawnHudMode = cfg.hud_mode;
    }

    if (cfg.hud_mode) {
        switch (currentPage) {
            case PAGE_NAV:  drawHudPageNav();  break;
            case PAGE_CALL: drawHudPageCall(); break;
            case PAGE_SMS:  drawHudPageSms();  break;
            default:        drawHudPageIdle(); break;
        }
    } else {
        drawTopBar();
        switch (currentPage) {
            case PAGE_NAV:  drawPageNav();  break;
            case PAGE_CALL: drawPageCall(); break;
            case PAGE_SMS:  drawPageSms();  break;
            default:        drawPageIdle(); break;
        }
        drawBottomBar();
    }
    needsRedraw = false;
}

// ============================================================
//  BLE Setup
// ============================================================
void setupBLE() {
    NimBLEDevice::init(DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(247);

    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    NimBLEService* pService = pServer->createService(SERVICE_UUID);

    NimBLECharacteristic* pRxChar = pService->createCharacteristic(
        CHAR_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pRxChar->setCallbacks(new RxCallbacks());

    pTxChar = pService->createCharacteristic(
        CHAR_TX_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
    );

    pService->start();

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(SERVICE_UUID);
    pAdv->setScanResponse(true);
    pAdv->start();

    Serial.println("[BLE] Advertising as " DEVICE_NAME);
}

// ============================================================
//  Arduino setup() / loop()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== Car HUD v3 starting ===");

    loadDisplayConfig();
    setupBacklight();

    tft.init();
    applyDisplayTransform();
    tft.fillScreen(COLOR_BG);

    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setTextFont(4);
    tft.setCursor(40, 60);
    tft.print("Car HUD v3");
    tft.setTextFont(2);
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setCursor(40, 100);
    tft.print("Initializing BLE...");

    setupBLE();
    delay(800);

    tft.fillScreen(COLOR_BG);
    needsRedraw = true;
}

void loop() {
    checkPageExpiration();
    render();
    delay(50);
}

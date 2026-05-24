// /**
//  * ESP32 Car HUD - BLE Receiver + ST7789 Display (v2)
//  *
//  * Fixes vs v1:
//  *   1. Distance unit (m / km) is now drawn next to the number.
//  *   2. Arrow type string is normalized (accepts "right", "turn-right",
//  *      "TURN_RIGHT", "0", etc.) and "left" / "uturn" now render correctly.
//  *      Unknown arrows fall back to a "?" badge in WARN color (not NAV green)
//  *      so it is obvious something is wrong, and the raw value is logged.
//  *   3. Nav layout reworked so long street names fit:
//  *        - Arrow on the left (compact, 32 px radius zone)
//  *        - Distance + unit on a single line, mid-size font
//  *        - Street name in 2 lines, smaller font, with word-wrap
//  *        - Automatic abbreviation of common Vietnamese phrases
//  *          ("Đến nơi lúc" -> "ETA", "Rẽ phải vào" -> "R:" ...) when
//  *          the text still overflows after wrapping.
//  *
//  * Compatible with: Arduino-ESP32 core 2.x + NimBLE-Arduino 1.4.x + ArduinoJson 6.x
//  *
//  * BLE Service:
//  *   Service UUID:        6e400001-b5a3-f393-e0a9-e50e24dcca9e
//  *   RX Characteristic:   6e400002-b5a3-f393-e0a9-e50e24dcca9e (WRITE)
//  *   TX Characteristic:   6e400003-b5a3-f393-e0a9-e50e24dcca9e (NOTIFY)
//  */

// #include <Arduino.h>
// #include <TFT_eSPI.h>
// #include <ArduinoJson.h>
// #include <NimBLEDevice.h>
// #include <ctype.h>
// #include <string.h>

// // ============================================================
// //  BLE UUIDs
// // ============================================================
// #define SERVICE_UUID  "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
// #define CHAR_RX_UUID  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
// #define CHAR_TX_UUID  "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
// #define DEVICE_NAME   "CarHUD-ESP32"

// // ============================================================
// //  Display & Colors
// // ============================================================
// TFT_eSPI tft = TFT_eSPI();

// #define COLOR_BG       0x0000
// #define COLOR_FG       0xFFFF
// #define COLOR_ACCENT   0xFD20  // orange
// #define COLOR_NAV      0x07E0  // green
// #define COLOR_SPEED    0xFFE0  // yellow
// #define COLOR_CALL     0x07FF  // cyan
// #define COLOR_SMS      0xF81F  // magenta
// #define COLOR_WARN     0xF800  // red
// #define COLOR_DIM      0x630C  // gray
// #define COLOR_HEADER   0x0841  // dark blue

// const int SCREEN_W = 320;
// const int SCREEN_H = 170;

// // ============================================================
// //  State machine
// // ============================================================
// enum Page {
//     PAGE_IDLE = 0,
//     PAGE_NAV  = 1,
//     PAGE_CALL = 2,
//     PAGE_SMS  = 3
// };

// // Normalized arrow types — handler converts raw string to this
// enum ArrowType {
//     ARR_NONE = 0,
//     ARR_STRAIGHT,
//     ARR_RIGHT,
//     ARR_LEFT,
//     ARR_SLIGHT_RIGHT,
//     ARR_SLIGHT_LEFT,
//     ARR_SHARP_RIGHT,
//     ARR_SHARP_LEFT,
//     ARR_UTURN_LEFT,
//     ARR_UTURN_RIGHT,
//     ARR_ARRIVE,      // "destination" / "arrived"
//     ARR_UNKNOWN
// };

// struct StateData {
//     ArrowType nav_arrow      = ARR_NONE;
//     char nav_arrow_raw[16]   = "";   // kept for debug
//     int  nav_dist            = 0;
//     char nav_unit[4]         = "m";
//     char nav_street[64]      = "";
//     unsigned long nav_until  = 0;

//     int  speed_kmh           = -1;
//     unsigned long speed_ts   = 0;

//     char call_name[32]       = "";
//     char call_phone[24]      = "";
//     unsigned long call_until = 0;

//     char sms_from[32]        = "";
//     char sms_msg[128]        = "";
//     unsigned long sms_until  = 0;

//     int  clk_hour            = -1;
//     int  clk_min             = -1;
//     unsigned long clk_ts     = 0;

//     int  bat_pct             = -1;
//     unsigned long bat_ts     = 0;

//     bool ble_connected       = false;
// };

// StateData st;
// Page currentPage   = PAGE_IDLE;
// Page lastDrawnPage = (Page)(-1);
// bool needsRedraw   = true;

// const unsigned long NAV_TIMEOUT  = 60000;
// const unsigned long CALL_TIMEOUT = 30000;
// const unsigned long SMS_TIMEOUT  = 15000;
// const unsigned long SPEED_STALE  = 10000;
// const unsigned long CLOCK_STALE  = 120000;
// const unsigned long BAT_STALE    = 120000;

// NimBLECharacteristic* pTxChar = nullptr;
// NimBLEServer*         pServer = nullptr;

// // ============================================================
// //  Arrow string normalization
// //  Accepts many spellings so the phone app does not have to
// //  match an exact constant. All comparisons are lowercase and
// //  ignore '_', '-', ' '.
// // ============================================================
// static void str_canon(const char* in, char* out, size_t outSize) {
//     size_t j = 0;
//     for (size_t i = 0; in[i] && j + 1 < outSize; i++) {
//         char c = in[i];
//         if (c == '-' || c == '_' || c == ' ') continue;
//         out[j++] = (char)tolower((unsigned char)c);
//     }
//     out[j] = '\0';
// }

// static ArrowType parseArrow(const char* raw) {
//     if (!raw || !*raw) return ARR_NONE;

//     char k[24];
//     str_canon(raw, k, sizeof(k));

//     // exact tokens used by typical nav SDKs (Google, Mapbox, OSRM)
//     if (!strcmp(k, "straight") || !strcmp(k, "up") ||
//         !strcmp(k, "continue") || !strcmp(k, "forward") ||
//         !strcmp(k, "depart"))                            return ARR_STRAIGHT;

//     if (!strcmp(k, "right")   || !strcmp(k, "turnright") ||
//         !strcmp(k, "turn0")   || !strcmp(k, "r"))         return ARR_RIGHT;

//     if (!strcmp(k, "left")    || !strcmp(k, "turnleft") ||
//         !strcmp(k, "l"))                                  return ARR_LEFT;

//     if (!strcmp(k, "slightright")  || !strcmp(k, "bearright") ||
//         !strcmp(k, "keepright"))                          return ARR_SLIGHT_RIGHT;

//     if (!strcmp(k, "slightleft")   || !strcmp(k, "bearleft") ||
//         !strcmp(k, "keepleft"))                           return ARR_SLIGHT_LEFT;

//     if (!strcmp(k, "sharpright"))                         return ARR_SHARP_RIGHT;
//     if (!strcmp(k, "sharpleft"))                          return ARR_SHARP_LEFT;

//     if (!strcmp(k, "uturn")        || !strcmp(k, "uturnleft") ||
//         !strcmp(k, "makeuturn"))                          return ARR_UTURN_LEFT;
//     if (!strcmp(k, "uturnright"))                         return ARR_UTURN_RIGHT;

//     if (!strcmp(k, "arrive")  || !strcmp(k, "arrived") ||
//         !strcmp(k, "destination") || !strcmp(k, "end"))   return ARR_ARRIVE;

//     return ARR_UNKNOWN;
// }

// // ============================================================
// //  BLE Server callbacks
// // ============================================================
// class ServerCallbacks : public NimBLEServerCallbacks {
//     void onConnect(NimBLEServer* pSrv, ble_gap_conn_desc* desc) override {
//         st.ble_connected = true;
//         needsRedraw = true;
//         Serial.printf("[BLE] Connected. Conn handle: %d\n", desc->conn_handle);
//         pSrv->updateConnParams(desc->conn_handle, 24, 48, 0, 60);
//     }
//     void onDisconnect(NimBLEServer* pSrv) override {
//         st.ble_connected = false;
//         needsRedraw = true;
//         Serial.println("[BLE] Disconnected. Restart advertising.");
//         NimBLEDevice::startAdvertising();
//     }
//     void onMTUChange(uint16_t MTU, ble_gap_conn_desc* desc) override {
//         Serial.printf("[BLE] MTU updated to %u for conn %d\n", MTU, desc->conn_handle);
//     }
// };

// // ============================================================
// //  JSON message handler
// // ============================================================
// void handleJsonMessage(const std::string& raw) {
//     Serial.printf("[RX] %s\n", raw.c_str());

//     StaticJsonDocument<512> doc;
//     DeserializationError err = deserializeJson(doc, raw);
//     if (err) {
//         Serial.printf("[JSON] parse error: %s\n", err.c_str());
//         return;
//     }

//     const char* type = doc["t"] | "";
//     unsigned long now = millis();

//     if (!strcmp(type, "nav")) {
//         const char* arrRaw = doc["arr"] | "";
//         strlcpy(st.nav_arrow_raw, arrRaw, sizeof(st.nav_arrow_raw));
//         st.nav_arrow = parseArrow(arrRaw);
//         if (st.nav_arrow == ARR_UNKNOWN) {
//             Serial.printf("[NAV] Unknown arrow string: '%s'\n", arrRaw);
//         }
//         st.nav_dist =          doc["d"]  | 0;
//         strlcpy(st.nav_unit,   doc["u"]  | "m", sizeof(st.nav_unit));
//         strlcpy(st.nav_street, doc["s"]  | "",  sizeof(st.nav_street));
//         st.nav_until = now + NAV_TIMEOUT;
//         currentPage = PAGE_NAV;
//         needsRedraw = true;
//     }
//     else if (!strcmp(type, "spd")) {
//         st.speed_kmh = doc["v"] | 0;
//         st.speed_ts  = now;
//         needsRedraw  = true;
//     }
//     else if (!strcmp(type, "call")) {
//         strlcpy(st.call_name,  doc["n"] | "Unknown", sizeof(st.call_name));
//         strlcpy(st.call_phone, doc["p"] | "",        sizeof(st.call_phone));
//         st.call_until = now + CALL_TIMEOUT;
//         currentPage = PAGE_CALL;
//         needsRedraw = true;
//     }
//     else if (!strcmp(type, "sms")) {
//         strlcpy(st.sms_from, doc["f"] | "Unknown", sizeof(st.sms_from));
//         strlcpy(st.sms_msg,  doc["m"] | "",        sizeof(st.sms_msg));
//         st.sms_until = now + SMS_TIMEOUT;
//         currentPage = PAGE_SMS;
//         needsRedraw = true;
//     }
//     else if (!strcmp(type, "clk")) {
//         st.clk_hour = doc["h"] | -1;
//         st.clk_min  = doc["m"] | -1;
//         st.clk_ts   = now;
//         needsRedraw = true;
//     }
//     else if (!strcmp(type, "bat")) {
//         st.bat_pct = doc["p"] | -1;
//         st.bat_ts  = now;
//         needsRedraw = true;
//     }
//     else if (!strcmp(type, "clr")) {
//         currentPage = PAGE_IDLE;
//         st.nav_until = 0;
//         st.call_until = 0;
//         st.sms_until = 0;
//         needsRedraw = true;
//     }
//     else {
//         Serial.printf("[JSON] unknown type: %s\n", type);
//     }
// }

// class RxCallbacks : public NimBLECharacteristicCallbacks {
//     void onWrite(NimBLECharacteristic* pChar) override {
//         std::string value = pChar->getValue();
//         if (!value.empty()) handleJsonMessage(value);
//     }
// };

// // ============================================================
// //  Arrow drawing — vector-based, no font hacks
// //  Each shape is drawn inside a (2s x 2s) box centered at (cx, cy).
// // ============================================================
// static void drawShaft(int cx, int cy, int len, int thick, uint16_t color) {
//     tft.fillRect(cx - thick / 2, cy - len / 2, thick, len, color);
// }

// void drawArrowShape(ArrowType type, int cx, int cy, int s, uint16_t color) {
//     switch (type) {
//         case ARR_RIGHT: {
//             // shaft pointing right
//             tft.fillRect(cx - s, cy - s / 4, s + s / 3, s / 2, color);
//             tft.fillTriangle(cx + s / 3, cy - s,
//                              cx + s / 3, cy + s,
//                              cx + s,     cy, color);
//             break;
//         }
//         case ARR_LEFT: {
//             tft.fillRect(cx - s / 3, cy - s / 4, s + s / 3, s / 2, color);
//             tft.fillTriangle(cx - s / 3, cy - s,
//                              cx - s / 3, cy + s,
//                              cx - s,     cy, color);
//             break;
//         }
//         case ARR_STRAIGHT: {
//             tft.fillRect(cx - s / 4, cy - s / 2, s / 2, s + s / 3, color);
//             tft.fillTriangle(cx - s, cy - s / 2,
//                              cx + s, cy - s / 2,
//                              cx,     cy - s, color);
//             break;
//         }
//         case ARR_SLIGHT_RIGHT: {
//             // diagonal up-right shaft + head
//             tft.fillTriangle(cx - s / 2, cy + s,
//                              cx + s,     cy - s / 2,
//                              cx + s / 2, cy - s / 4 + s / 4, color);
//             // head: small triangle at the top-right
//             tft.fillTriangle(cx + s,     cy - s,
//                              cx + s / 4, cy - s / 2,
//                              cx + s,     cy - s / 4, color);
//             break;
//         }
//         case ARR_SLIGHT_LEFT: {
//             tft.fillTriangle(cx + s / 2, cy + s,
//                              cx - s,     cy - s / 2,
//                              cx - s / 2, cy - s / 4 + s / 4, color);
//             tft.fillTriangle(cx - s,     cy - s,
//                              cx - s / 4, cy - s / 2,
//                              cx - s,     cy - s / 4, color);
//             break;
//         }
//         case ARR_SHARP_RIGHT: {
//             // L-shape: short vertical going up, then strong right turn
//             tft.fillRect(cx - s / 4, cy - s / 8, s / 2, s + s / 8, color);
//             tft.fillRect(cx - s / 4, cy - s / 8, s, s / 2, color);
//             tft.fillTriangle(cx + s - s / 8, cy - s / 2,
//                              cx + s - s / 8, cy + s / 2,
//                              cx + s + s / 3, cy + s / 8, color);
//             break;
//         }
//         case ARR_SHARP_LEFT: {
//             tft.fillRect(cx - s / 4, cy - s / 8, s / 2, s + s / 8, color);
//             tft.fillRect(cx - s,     cy - s / 8, s + s / 4, s / 2, color);
//             tft.fillTriangle(cx - s + s / 8, cy - s / 2,
//                              cx - s + s / 8, cy + s / 2,
//                              cx - s - s / 3, cy + s / 8, color);
//             break;
//         }
//         case ARR_UTURN_LEFT: {
//             // come up from the bottom-right, loop over the top, point down-left
//             tft.fillRect(cx + s / 3,        cy - s / 4, s / 3, s + s / 4, color);     // right vertical
//             tft.fillRect(cx - s / 2,        cy - s,     s + s / 6, s / 3, color);     // top horizontal
//             tft.fillRect(cx - s / 2,        cy - s,     s / 3, s / 2 + s / 8, color); // left vertical (short)
//             // head pointing down-left
//             tft.fillTriangle(cx - s / 2 - s / 3, cy - s / 8,
//                              cx - s / 2 + s / 3, cy - s / 8,
//                              cx - s / 6,         cy + s / 2, color);
//             break;
//         }
//         case ARR_UTURN_RIGHT: {
//             // mirror of UTURN_LEFT
//             tft.fillRect(cx - s / 3 - s / 3, cy - s / 4, s / 3, s + s / 4, color);
//             tft.fillRect(cx - s / 3,         cy - s,     s + s / 6, s / 3, color);
//             tft.fillRect(cx + s / 6,         cy - s,     s / 3, s / 2 + s / 8, color);
//             tft.fillTriangle(cx + s / 6 - s / 3, cy - s / 8,
//                              cx + s / 6 + s / 3, cy - s / 8,
//                              cx + s / 6 + s / 6, cy + s / 2, color);
//             break;
//         }
//         case ARR_ARRIVE: {
//             // flag / checkered destination marker
//             drawShaft(cx, cy + s / 4, s + s / 2, s / 6, color);
//             tft.fillRect(cx, cy - s, s, s / 2 + s / 4, color);
//             // checker squares (dark holes)
//             int sq = s / 5;
//             tft.fillRect(cx + sq,     cy - s + sq, sq, sq, COLOR_BG);
//             tft.fillRect(cx + 3 * sq, cy - s + sq, sq, sq, COLOR_BG);
//             tft.fillRect(cx,          cy - s,      sq, sq, COLOR_BG);
//             tft.fillRect(cx + 2 * sq, cy - s,      sq, sq, COLOR_BG);
//             break;
//         }
//         default: {
//             // unknown / none: red badge with "?"
//             tft.fillCircle(cx, cy, s, COLOR_WARN);
//             tft.setTextColor(COLOR_FG, COLOR_WARN);
//             tft.setTextFont(4);
//             tft.setCursor(cx - 6, cy - 12);
//             tft.print("?");
//             break;
//         }
//     }
// }

// // ============================================================
// //  Top / bottom bars
// // ============================================================
// void drawTopBar() {
//     tft.fillRect(0, 0, SCREEN_W, 18, COLOR_HEADER);
//     tft.setTextColor(COLOR_FG, COLOR_HEADER);
//     tft.setTextFont(1);
//     tft.setTextSize(1);
//     tft.setCursor(6, 4);
//     tft.print(st.ble_connected ? "BLE OK" : "BLE...");

//     const char* tag = "READY";
//     switch (currentPage) {
//         case PAGE_NAV:  tag = "NAVIGATION";    break;
//         case PAGE_CALL: tag = "INCOMING CALL"; break;
//         case PAGE_SMS:  tag = "MESSAGE";       break;
//         default:        tag = "READY";         break;
//     }
//     int tagW = strlen(tag) * 6;
//     tft.setCursor(SCREEN_W - tagW - 6, 4);
//     tft.print(tag);
// }

// void drawBottomBar() {
//     const int barY = SCREEN_H - 22;
//     tft.fillRect(0, barY, SCREEN_W, 22, COLOR_BG);
//     tft.drawFastHLine(0, barY, SCREEN_W, COLOR_DIM);

//     unsigned long now = millis();
//     char buf[16];

//     // speed
//     tft.setTextFont(2);
//     if (st.speed_kmh >= 0 && (now - st.speed_ts < SPEED_STALE)) {
//         tft.setTextColor(COLOR_SPEED, COLOR_BG);
//         snprintf(buf, sizeof(buf), "%d km/h", st.speed_kmh);
//     } else {
//         tft.setTextColor(COLOR_DIM, COLOR_BG);
//         snprintf(buf, sizeof(buf), "-- km/h");
//     }
//     tft.setCursor(8, barY + 3);
//     tft.print(buf);

//     // clock (centered)
//     if (st.clk_hour >= 0 && (now - st.clk_ts < CLOCK_STALE)) {
//         tft.setTextColor(COLOR_FG, COLOR_BG);
//         snprintf(buf, sizeof(buf), "%02d:%02d", st.clk_hour, st.clk_min);
//     } else {
//         tft.setTextColor(COLOR_DIM, COLOR_BG);
//         snprintf(buf, sizeof(buf), "--:--");
//     }
//     int clkW = tft.textWidth(buf);
//     tft.setCursor((SCREEN_W - clkW) / 2, barY + 3);
//     tft.print(buf);

//     // battery (right)
//     if (st.bat_pct >= 0 && (now - st.bat_ts < BAT_STALE)) {
//         if (st.bat_pct <= 20) tft.setTextColor(COLOR_WARN, COLOR_BG);
//         else                  tft.setTextColor(COLOR_FG,   COLOR_BG);
//         snprintf(buf, sizeof(buf), "Bat %d%%", st.bat_pct);
//     } else {
//         tft.setTextColor(COLOR_DIM, COLOR_BG);
//         snprintf(buf, sizeof(buf), "Bat --");
//     }
//     int batW = tft.textWidth(buf);
//     tft.setCursor(SCREEN_W - batW - 8, barY + 3);
//     tft.print(buf);
// }

// void drawPageIdle() {
//     int contentY = 18;
//     int contentH = SCREEN_H - 22 - 18;
//     tft.fillRect(0, contentY, SCREEN_W, contentH, COLOR_BG);

//     tft.setTextColor(COLOR_DIM, COLOR_BG);
//     tft.setTextFont(4);
//     const char* msg1 = "Car HUD Ready";
//     int w1 = tft.textWidth(msg1);
//     tft.setCursor((SCREEN_W - w1) / 2, contentY + 25);
//     tft.print(msg1);

//     tft.setTextFont(2);
//     const char* msg2 = st.ble_connected
//                          ? "Waiting for data..."
//                          : "Connect from your phone";
//     int w2 = tft.textWidth(msg2);
//     tft.setCursor((SCREEN_W - w2) / 2, contentY + 70);
//     tft.print(msg2);
// }

// // ============================================================
// //  Nav page — re-laid-out
// //
// //  Layout (320 x 130 content area, between top bar y=18 and
// //  bottom bar y=148):
// //
// //   +-------------------------------------------------+ 18
// //   |          |                                      |
// //   |          |   320 m                              |
// //   |  ARROW   |   --------------------------------   |
// //   |          |   Den noi luc 09:35                  |
// //   |          |   Nguyen Trai                        |
// //   +-------------------------------------------------+ 148
// //
// //   Arrow zone:  x = 0..95,   centered at (48, 83), size = 32
// //   Distance:    x = 100,     y = 30,  font 6 (digits) + font 4 (unit)
// //   Street:      x = 100,     y = 78,  font 2, 2 lines max, with
// //                wrap on word boundary and Vietnamese-aware
// //                abbreviation if it still doesn't fit.
// // ============================================================

// // Abbreviate common long phrases on overflow.
// // Returns true if anything was changed.
// static bool abbreviateStreet(char* s, size_t n) {
//     struct Sub { const char* from; const char* to; };
//     static const Sub subs[] = {
//         // Vietnamese (raw ASCII — many phones strip diacritics anyway)
//         { "Den noi luc",   "ETA"   },
//         { "den noi luc",   "ETA"   },
//         { "Re phai vao",   "R:"    },
//         { "Re trai vao",   "L:"    },
//         { "Re phai",       "R"     },
//         { "Re trai",       "L"     },
//         { "Di thang",      "Fwd"   },
//         { "Quay dau",      "U-turn"},
//         { "Duong",         "D."    },
//         { "Pho",           "P."    },
//         { "Quan",          "Q."    },
//         // English / generic
//         { "Continue on",   "Cont." },
//         { "Turn right onto", "R:"  },
//         { "Turn left onto",  "L:"  },
//         { "Turn right",    "R"     },
//         { "Turn left",     "L"     },
//         { "Street",        "St"    },
//         { "Avenue",        "Ave"   },
//         { "Boulevard",     "Blvd"  },
//         { "Road",          "Rd"    },
//     };
//     bool changed = false;
//     for (size_t i = 0; i < sizeof(subs)/sizeof(subs[0]); i++) {
//         const char* needle = subs[i].from;
//         size_t nlen = strlen(needle);
//         size_t rlen = strlen(subs[i].to);
//         char* p = strstr(s, needle);
//         while (p) {
//             size_t tail = strlen(p + nlen);
//             if ((p - s) + rlen + tail + 1 > n) break;
//             memmove(p + rlen, p + nlen, tail + 1);
//             memcpy(p, subs[i].to, rlen);
//             changed = true;
//             p = strstr(p + rlen, needle);
//         }
//     }
//     return changed;
// }

// // Split `text` into up to 2 lines that each fit within maxW pixels using
// // current font. Word-breaks where possible. Truncates with ".." if even
// // 2 lines aren't enough.
// static void wrapTwoLines(const char* text, int maxW,
//                          char* line1, size_t l1Size,
//                          char* line2, size_t l2Size) {
//     line1[0] = '\0';
//     line2[0] = '\0';
//     if (!text || !*text) return;

//     // line 1: greedy word fit
//     char buf[160];
//     strlcpy(buf, text, sizeof(buf));

//     int len = strlen(buf);
//     int cut = len;
//     while (cut > 0) {
//         char saved = buf[cut];
//         buf[cut] = '\0';
//         if (tft.textWidth(buf) <= maxW) {
//             buf[cut] = saved;
//             break;
//         }
//         buf[cut] = saved;
//         cut--;
//     }
//     if (cut < len) {
//         int back = cut;
//         while (back > 1 && buf[back] != ' ') back--;
//         if (back > len / 2) cut = back;
//     }
//     strlcpy(line1, buf, l1Size);
//     line1[cut < (int)l1Size ? cut : (int)l1Size - 1] = '\0';

//     if (cut >= len) return;

//     // line 2: rest, possibly truncated
//     const char* rest = text + cut;
//     while (*rest == ' ') rest++;
//     strlcpy(line2, rest, l2Size);

//     // truncate line2 with ".." if too wide
//     while (tft.textWidth(line2) > maxW && strlen(line2) > 2) {
//         size_t L = strlen(line2);
//         line2[L - 1] = '\0';
//         if (L >= 3) {
//             line2[L - 3] = '.';
//             line2[L - 2] = '.';
//             line2[L - 1] = '\0';
//         }
//     }
// }

// void drawPageNav() {
//     int contentY = 18;
//     int contentH = SCREEN_H - 22 - 18;  // 130
//     tft.fillRect(0, contentY, SCREEN_W, contentH, COLOR_BG);

//     // ---- Arrow (left zone, 0..95) ----
//     const int arrowCX = 48;
//     const int arrowCY = contentY + contentH / 2;  // 83
//     const int arrowS  = 32;

//     uint16_t arrowColor = (st.nav_arrow == ARR_UNKNOWN || st.nav_arrow == ARR_NONE)
//                            ? COLOR_WARN : COLOR_NAV;
//     drawArrowShape(st.nav_arrow, arrowCX, arrowCY, arrowS, arrowColor);

//     // ---- Distance + unit (single line) ----
//     const int textX  = 100;
//     char distNum[16];
//     snprintf(distNum, sizeof(distNum), "%d", st.nav_dist);

//     // Font 6: digits/colon only, large. Use it for the number.
//     tft.setTextColor(COLOR_FG, COLOR_BG);
//     tft.setTextFont(6);
//     int numY = contentY + 12;
//     tft.setCursor(textX, numY);
//     tft.print(distNum);
//     int numW = tft.textWidth(distNum);

//     // Font 4: regular text, for the unit. Place right after the number.
//     tft.setTextFont(4);
//     tft.setTextColor(COLOR_ACCENT, COLOR_BG);
//     // font 6 is taller (~48px) than font 4 (~26px); baseline-align by adding offset
//     int unitY = numY + 22;
//     tft.setCursor(textX + numW + 6, unitY);
//     tft.print(st.nav_unit);

//     // ---- Street name (up to 2 lines) ----
//     tft.setTextFont(2);
//     tft.setTextColor(COLOR_ACCENT, COLOR_BG);

//     int streetMaxW = SCREEN_W - textX - 8;  // available width: ~212 px
//     char working[96];
//     strlcpy(working, st.nav_street, sizeof(working));

//     // First try direct wrap. If line2 ends up truncated, abbreviate and re-wrap.
//     char l1[64], l2[64];
//     wrapTwoLines(working, streetMaxW, l1, sizeof(l1), l2, sizeof(l2));

//     bool overflowed = (strstr(l2, "..") != nullptr);
//     if (overflowed && abbreviateStreet(working, sizeof(working))) {
//         wrapTwoLines(working, streetMaxW, l1, sizeof(l1), l2, sizeof(l2));
//     }

//     int streetY = contentY + 78;
//     tft.setCursor(textX, streetY);
//     tft.print(l1);
//     if (l2[0]) {
//         tft.setCursor(textX, streetY + 18);
//         tft.print(l2);
//     }
// }

// void drawPageCall() {
//     int contentY = 18;
//     int contentH = SCREEN_H - 22 - 18;
//     tft.fillRect(0, contentY, SCREEN_W, contentH, COLOR_BG);

//     int icx = 50, icy = contentY + 50;
//     tft.fillCircle(icx, icy, 26, COLOR_CALL);
//     tft.fillCircle(icx, icy, 20, COLOR_BG);
//     tft.fillRoundRect(icx - 12, icy - 4, 24, 12, 4, COLOR_CALL);
//     tft.fillRect(icx - 14, icy - 12, 6, 10, COLOR_CALL);
//     tft.fillRect(icx + 8,  icy - 12, 6, 10, COLOR_CALL);

//     tft.setTextColor(COLOR_FG, COLOR_BG);
//     tft.setTextFont(4);
//     tft.setCursor(100, contentY + 18);
//     char nameBuf[32];
//     strlcpy(nameBuf, st.call_name, sizeof(nameBuf));
//     while (tft.textWidth(nameBuf) > (SCREEN_W - 100 - 8) && strlen(nameBuf) > 3) {
//         nameBuf[strlen(nameBuf) - 1] = '\0';
//     }
//     tft.print(nameBuf);

//     tft.setTextColor(COLOR_DIM, COLOR_BG);
//     tft.setTextFont(2);
//     tft.setCursor(100, contentY + 60);
//     tft.print(st.call_phone);

//     tft.setTextColor(COLOR_CALL, COLOR_BG);
//     tft.setCursor(100, contentY + 90);
//     tft.print("Incoming call");
// }

// void drawPageSms() {
//     int contentY = 18;
//     int contentH = SCREEN_H - 22 - 18;
//     tft.fillRect(0, contentY, SCREEN_W, contentH, COLOR_BG);

//     tft.fillRect(0, contentY, SCREEN_W, 24, COLOR_SMS);
//     tft.setTextColor(COLOR_BG, COLOR_SMS);
//     tft.setTextFont(2);
//     char fromBuf[40];
//     snprintf(fromBuf, sizeof(fromBuf), "  Msg from %s", st.sms_from);
//     tft.setCursor(4, contentY + 5);
//     tft.print(fromBuf);

//     tft.setTextColor(COLOR_FG, COLOR_BG);
//     tft.setTextFont(2);
//     const int bodyY = contentY + 32;
//     const int lineH = 18;
//     const int maxLines = 4;
//     const int rightMargin = SCREEN_W - 10;

//     char remaining[128];
//     strlcpy(remaining, st.sms_msg, sizeof(remaining));

//     for (int line = 0; line < maxLines && strlen(remaining) > 0; line++) {
//         char lineBuf[128];
//         strlcpy(lineBuf, remaining, sizeof(lineBuf));

//         while (tft.textWidth(lineBuf) > (rightMargin - 8) && strlen(lineBuf) > 1) {
//             lineBuf[strlen(lineBuf) - 1] = '\0';
//         }
//         if (line < maxLines - 1 && strlen(lineBuf) < strlen(remaining)) {
//             int len = strlen(lineBuf);
//             for (int i = len - 1; i > len / 2; i--) {
//                 if (lineBuf[i] == ' ') { lineBuf[i] = '\0'; break; }
//             }
//         }
//         tft.setCursor(8, bodyY + line * lineH);
//         tft.print(lineBuf);

//         size_t consumed = strlen(lineBuf);
//         while (remaining[consumed] == ' ') consumed++;
//         memmove(remaining, remaining + consumed, strlen(remaining) - consumed + 1);
//     }
// }

// void checkPageExpiration() {
//     unsigned long now = millis();
//     Page next = currentPage;

//     if (currentPage == PAGE_SMS && st.sms_until && now > st.sms_until) {
//         st.sms_until = 0;
//         next = (st.nav_until && now < st.nav_until) ? PAGE_NAV : PAGE_IDLE;
//     }
//     if (currentPage == PAGE_CALL && st.call_until && now > st.call_until) {
//         st.call_until = 0;
//         next = (st.nav_until && now < st.nav_until) ? PAGE_NAV : PAGE_IDLE;
//     }
//     if (currentPage == PAGE_NAV && st.nav_until && now > st.nav_until) {
//         st.nav_until = 0;
//         next = PAGE_IDLE;
//     }

//     if (next != currentPage) {
//         currentPage = next;
//         needsRedraw = true;
//     }
// }

// void render() {
//     if (!needsRedraw && currentPage == lastDrawnPage) return;

//     if (currentPage != lastDrawnPage) {
//         tft.fillScreen(COLOR_BG);
//         lastDrawnPage = currentPage;
//     }
//     drawTopBar();
//     switch (currentPage) {
//         case PAGE_NAV:  drawPageNav();  break;
//         case PAGE_CALL: drawPageCall(); break;
//         case PAGE_SMS:  drawPageSms();  break;
//         default:        drawPageIdle(); break;
//     }
//     drawBottomBar();
//     needsRedraw = false;
// }

// // ============================================================
// //  BLE Setup
// // ============================================================
// void setupBLE() {
//     NimBLEDevice::init(DEVICE_NAME);
//     NimBLEDevice::setPower(ESP_PWR_LVL_P9);
//     NimBLEDevice::setMTU(247);

//     pServer = NimBLEDevice::createServer();
//     pServer->setCallbacks(new ServerCallbacks());

//     NimBLEService* pService = pServer->createService(SERVICE_UUID);

//     NimBLECharacteristic* pRxChar = pService->createCharacteristic(
//         CHAR_RX_UUID,
//         NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
//     );
//     pRxChar->setCallbacks(new RxCallbacks());

//     pTxChar = pService->createCharacteristic(
//         CHAR_TX_UUID,
//         NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
//     );

//     pService->start();

//     NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
//     pAdv->addServiceUUID(SERVICE_UUID);
//     pAdv->setScanResponse(true);
//     pAdv->start();

//     Serial.println("[BLE] Advertising as " DEVICE_NAME);
// }

// // ============================================================
// //  Arduino setup() / loop()
// // ============================================================
// void setup() {
//     Serial.begin(115200);
//     delay(1000);
//     Serial.println("\n=== Car HUD v2 starting ===");

//     pinMode(TFT_BL, OUTPUT);
//     digitalWrite(TFT_BL, HIGH);
//     tft.init();
//     tft.setRotation(1);
//     tft.fillScreen(COLOR_BG);

//     tft.setTextColor(COLOR_ACCENT, COLOR_BG);
//     tft.setTextFont(4);
//     tft.setCursor(40, 60);
//     tft.print("Car HUD v2");
//     tft.setTextFont(2);
//     tft.setTextColor(COLOR_DIM, COLOR_BG);
//     tft.setCursor(40, 100);
//     tft.print("Initializing BLE...");

//     setupBLE();
//     delay(800);

//     tft.fillScreen(COLOR_BG);
//     needsRedraw = true;
// }

// void loop() {
//     checkPageExpiration();
//     render();
//     delay(50);
// }

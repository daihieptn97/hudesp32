/**
 * ESP32 Car HUD - BLE Receiver + ST7789 Display
 * Compatible with: Arduino-ESP32 core 2.x + NimBLE-Arduino 1.4.x + ArduinoJson 6.x
 *
 * BLE Service:
 *   Service UUID:        6e400001-b5a3-f393-e0a9-e50e24dcca9e
 *   RX Characteristic:   6e400002-b5a3-f393-e0a9-e50e24dcca9e (WRITE)
 *   TX Characteristic:   6e400003-b5a3-f393-e0a9-e50e24dcca9e (NOTIFY)
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

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
#define COLOR_ACCENT   0xFD20
#define COLOR_NAV      0x07E0
#define COLOR_SPEED    0xFFE0
#define COLOR_CALL     0x07FF
#define COLOR_SMS      0xF81F
#define COLOR_WARN     0xF800
#define COLOR_DIM      0x630C
#define COLOR_HEADER   0x0841

const int SCREEN_W = 320;
const int SCREEN_H = 170;

// ============================================================
//  State machine (multi-page UI)
// ============================================================
enum Page {
    PAGE_IDLE   = 0,
    PAGE_NAV    = 1,
    PAGE_CALL   = 2,
    PAGE_SMS    = 3
};

struct StateData {
    char nav_arrow[8]      = "";
    int  nav_dist          = 0;
    char nav_unit[4]       = "m";
    char nav_street[48]    = "";
    unsigned long nav_until = 0;

    int  speed_kmh         = -1;
    unsigned long speed_ts = 0;

    char call_name[32]     = "";
    char call_phone[24]    = "";
    unsigned long call_until = 0;

    char sms_from[32]      = "";
    char sms_msg[128]      = "";
    unsigned long sms_until = 0;

    int  clk_hour          = -1;
    int  clk_min           = -1;
    unsigned long clk_ts   = 0;

    int  bat_pct           = -1;
    unsigned long bat_ts   = 0;

    bool ble_connected     = false;
};

StateData st;
Page currentPage    = PAGE_IDLE;
Page lastDrawnPage  = (Page)(-1);
bool needsRedraw    = true;

const unsigned long NAV_TIMEOUT  = 60000;
const unsigned long CALL_TIMEOUT = 30000;
const unsigned long SMS_TIMEOUT  = 15000;
const unsigned long SPEED_STALE  = 10000;
const unsigned long CLOCK_STALE  = 120000;
const unsigned long BAT_STALE    = 120000;

NimBLECharacteristic* pTxChar = nullptr;
NimBLEServer*         pServer = nullptr;

// ============================================================
//  BLE Server callbacks (NimBLE 1.x API)
// ============================================================
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pSrv, ble_gap_conn_desc* desc) override {
        st.ble_connected = true;
        needsRedraw = true;
        Serial.printf("[BLE] Connected. Conn handle: %d\n", desc->conn_handle);
        // Update connection params for stability
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
//  JSON message handler (ArduinoJson 6.x API)
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

    if (strcmp(type, "nav") == 0) {
        strlcpy(st.nav_arrow,  doc["arr"] | "",  sizeof(st.nav_arrow));
        st.nav_dist =          doc["d"]   | 0;
        strlcpy(st.nav_unit,   doc["u"]   | "m", sizeof(st.nav_unit));
        strlcpy(st.nav_street, doc["s"]   | "",  sizeof(st.nav_street));
        st.nav_until = now + NAV_TIMEOUT;
        currentPage = PAGE_NAV;
        needsRedraw = true;
    }
    else if (strcmp(type, "spd") == 0) {
        st.speed_kmh = doc["v"] | 0;
        st.speed_ts  = now;
        needsRedraw  = true;
    }
    else if (strcmp(type, "call") == 0) {
        strlcpy(st.call_name,  doc["n"] | "Unknown", sizeof(st.call_name));
        strlcpy(st.call_phone, doc["p"] | "",        sizeof(st.call_phone));
        st.call_until = now + CALL_TIMEOUT;
        currentPage = PAGE_CALL;
        needsRedraw = true;
    }
    else if (strcmp(type, "sms") == 0) {
        strlcpy(st.sms_from, doc["f"] | "Unknown", sizeof(st.sms_from));
        strlcpy(st.sms_msg,  doc["m"] | "",        sizeof(st.sms_msg));
        st.sms_until = now + SMS_TIMEOUT;
        currentPage = PAGE_SMS;
        needsRedraw = true;
    }
    else if (strcmp(type, "clk") == 0) {
        st.clk_hour = doc["h"] | -1;
        st.clk_min  = doc["m"] | -1;
        st.clk_ts   = now;
        needsRedraw = true;
    }
    else if (strcmp(type, "bat") == 0) {
        st.bat_pct = doc["p"] | -1;
        st.bat_ts  = now;
        needsRedraw = true;
    }
    else if (strcmp(type, "clr") == 0) {
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

// NimBLE 1.x callback API
class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar) override {
        std::string value = pChar->getValue();
        if (!value.empty()) {
            handleJsonMessage(value);
        }
    }
};

// ============================================================
//  Drawing helpers (same as before)
// ============================================================
void drawArrow(const char* arrow, int cx, int cy, int size, uint16_t color) {
    int s = size;

    if (strcmp(arrow, "right") == 0) {
        tft.fillRect(cx - s, cy - s/4, s + s/2, s/2, color);
        tft.fillTriangle(cx + s/2, cy - s, cx + s/2, cy + s, cx + s, cy, color);
    }
    else if (strcmp(arrow, "left") == 0) {
        tft.fillRect(cx - s/2, cy - s/4, s + s/2, s/2, color);
        tft.fillTriangle(cx - s/2, cy - s, cx - s/2, cy + s, cx - s, cy, color);
    }
    else if (strcmp(arrow, "straight") == 0 || strcmp(arrow, "up") == 0) {
        tft.fillRect(cx - s/4, cy - s/2, s/2, s + s/2, color);
        tft.fillTriangle(cx - s, cy - s/2, cx + s, cy - s/2, cx, cy - s, color);
    }
    else if (strcmp(arrow, "uturn") == 0) {
        tft.fillRect(cx + s/4, cy - s/2, s/3, s + s/2, color);
        tft.fillRect(cx - s, cy - s, s + s/2, s/3, color);
        tft.fillRect(cx - s, cy - s/2, s/3, s/2, color);
        tft.fillTriangle(cx - s - s/4, cy - s/4,
                         cx - s/4,     cy - s/4,
                         cx - s + s/8, cy + s/2, color);
    }
    else if (strcmp(arrow, "slight-right") == 0) {
        tft.fillTriangle(cx - s, cy + s,
                         cx + s, cy - s,
                         cx + s - s/2, cy - s + s/2 + s/3, color);
        tft.fillTriangle(cx + s, cy - s,
                         cx + s - s/2 - s/3, cy - s + s/2,
                         cx + s - s/2, cy - s + s/2 + s/3, color);
    }
    else if (strcmp(arrow, "slight-left") == 0) {
        tft.fillTriangle(cx + s, cy + s,
                         cx - s, cy - s,
                         cx - s + s/2, cy - s + s/2 + s/3, color);
        tft.fillTriangle(cx - s, cy - s,
                         cx - s + s/2 + s/3, cy - s + s/2,
                         cx - s + s/2, cy - s + s/2 + s/3, color);
    }
    else {
        tft.fillCircle(cx, cy, s, color);
        tft.setTextColor(COLOR_BG, color);
        tft.drawString("?", cx - 6, cy - 8, 4);
    }
}

void drawTopBar() {
    tft.fillRect(0, 0, SCREEN_W, 18, COLOR_HEADER);
    tft.setTextColor(COLOR_FG, COLOR_HEADER);
    tft.setCursor(6, 4);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.print(st.ble_connected ? "BLE OK" : "BLE...");

    const char* tag = "READY";
    switch (currentPage) {
        case PAGE_NAV:  tag = "NAVIGATION"; break;
        case PAGE_CALL: tag = "INCOMING CALL"; break;
        case PAGE_SMS:  tag = "MESSAGE"; break;
        default:        tag = "READY"; break;
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

    tft.setTextColor(COLOR_SPEED, COLOR_BG);
    tft.setTextFont(2);
    if (st.speed_kmh >= 0 && (now - st.speed_ts < SPEED_STALE)) {
        snprintf(buf, sizeof(buf), "%d km/h", st.speed_kmh);
    } else {
        snprintf(buf, sizeof(buf), "-- km/h");
        tft.setTextColor(COLOR_DIM, COLOR_BG);
    }
    tft.setCursor(8, barY + 3);
    tft.print(buf);

    tft.setTextColor(COLOR_FG, COLOR_BG);
    if (st.clk_hour >= 0 && (now - st.clk_ts < CLOCK_STALE)) {
        snprintf(buf, sizeof(buf), "%02d:%02d", st.clk_hour, st.clk_min);
    } else {
        snprintf(buf, sizeof(buf), "--:--");
        tft.setTextColor(COLOR_DIM, COLOR_BG);
    }
    int clkW = strlen(buf) * 11;
    tft.setCursor((SCREEN_W - clkW) / 2, barY + 3);
    tft.print(buf);

    tft.setTextColor(COLOR_FG, COLOR_BG);
    if (st.bat_pct >= 0 && (now - st.bat_ts < BAT_STALE)) {
        snprintf(buf, sizeof(buf), "Bat %d%%", st.bat_pct);
        if (st.bat_pct <= 20) tft.setTextColor(COLOR_WARN, COLOR_BG);
    } else {
        snprintf(buf, sizeof(buf), "Bat --");
        tft.setTextColor(COLOR_DIM, COLOR_BG);
    }
    int batW = strlen(buf) * 11;
    tft.setCursor(SCREEN_W - batW - 8, barY + 3);
    tft.print(buf);
}

void drawPageIdle() {
    int contentY = 18;
    int contentH = SCREEN_H - 22 - 18;
    tft.fillRect(0, contentY, SCREEN_W, contentH, COLOR_BG);

    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextFont(4);
    const char* msg1 = "Car HUD Ready";
    int w1 = tft.textWidth(msg1);
    tft.setCursor((SCREEN_W - w1) / 2, contentY + 30);
    tft.print(msg1);

    tft.setTextFont(2);
    const char* msg2 = st.ble_connected
                         ? "Waiting for data..."
                         : "Connect from your phone";
    int w2 = tft.textWidth(msg2);
    tft.setCursor((SCREEN_W - w2) / 2, contentY + 75);
    tft.print(msg2);
}

void drawPageNav() {
    int contentY = 18;
    int contentH = SCREEN_H - 22 - 18;
    tft.fillRect(0, contentY, SCREEN_W, contentH, COLOR_BG);

    int arrowCX = 70;
    int arrowCY = contentY + contentH / 2;
    drawArrow(st.nav_arrow, arrowCX, arrowCY, 38, COLOR_NAV);

    char distBuf[16];
    snprintf(distBuf, sizeof(distBuf), "%d %s", st.nav_dist, st.nav_unit);
    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextFont(7);
    tft.setCursor(150, contentY + 15);
    tft.print(distBuf);

    tft.setTextFont(4);
    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    char streetBuf[40];
    strlcpy(streetBuf, st.nav_street, sizeof(streetBuf));
    while (tft.textWidth(streetBuf) > (SCREEN_W - 150 - 8) && strlen(streetBuf) > 3) {
        streetBuf[strlen(streetBuf) - 1] = '\0';
    }
    tft.setCursor(150, contentY + 80);
    tft.print(streetBuf);
}

void drawPageCall() {
    int contentY = 18;
    int contentH = SCREEN_H - 22 - 18;
    tft.fillRect(0, contentY, SCREEN_W, contentH, COLOR_BG);

    int icx = 50, icy = contentY + 50;
    tft.fillCircle(icx, icy, 26, COLOR_CALL);
    tft.fillCircle(icx, icy, 20, COLOR_BG);
    tft.fillRoundRect(icx - 12, icy - 4, 24, 12, 4, COLOR_CALL);
    tft.fillRect(icx - 14, icy - 12, 6, 10, COLOR_CALL);
    tft.fillRect(icx + 8,  icy - 12, 6, 10, COLOR_CALL);

    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextFont(4);
    tft.setCursor(100, contentY + 18);
    char nameBuf[32];
    strlcpy(nameBuf, st.call_name, sizeof(nameBuf));
    while (tft.textWidth(nameBuf) > (SCREEN_W - 100 - 8) && strlen(nameBuf) > 3) {
        nameBuf[strlen(nameBuf) - 1] = '\0';
    }
    tft.print(nameBuf);

    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.setTextFont(2);
    tft.setCursor(100, contentY + 60);
    tft.print(st.call_phone);

    tft.setTextColor(COLOR_CALL, COLOR_BG);
    tft.setCursor(100, contentY + 90);
    tft.print("Incoming call");
}

void drawPageSms() {
    int contentY = 18;
    int contentH = SCREEN_H - 22 - 18;
    tft.fillRect(0, contentY, SCREEN_W, contentH, COLOR_BG);

    tft.fillRect(0, contentY, SCREEN_W, 26, COLOR_SMS);
    tft.setTextColor(COLOR_BG, COLOR_SMS);
    tft.setTextFont(2);
    char fromBuf[40];
    snprintf(fromBuf, sizeof(fromBuf), "  Msg from %s", st.sms_from);
    tft.setCursor(4, contentY + 6);
    tft.print(fromBuf);

    tft.setTextColor(COLOR_FG, COLOR_BG);
    tft.setTextFont(4);
    const int bodyY = contentY + 34;
    const int lineH = 24;
    const int maxLines = 3;
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

        int consumed = strlen(lineBuf);
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
    if (!needsRedraw && currentPage == lastDrawnPage) return;

    if (currentPage != lastDrawnPage) {
        tft.fillScreen(COLOR_BG);
        drawTopBar();
        switch (currentPage) {
            case PAGE_NAV:  drawPageNav();  break;
            case PAGE_CALL: drawPageCall(); break;
            case PAGE_SMS:  drawPageSms();  break;
            default:        drawPageIdle(); break;
        }
        drawBottomBar();
        lastDrawnPage = currentPage;
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
//  BLE Setup (NimBLE 1.x API)
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
    Serial.println("\n=== Car HUD starting ===");

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(COLOR_BG);

    tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    tft.setTextFont(4);
    tft.setCursor(40, 60);
    tft.print("Car HUD");
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
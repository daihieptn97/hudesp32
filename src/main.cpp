// PlatformIO required include
#include <Arduino.h>

#include <TFT_eSPI.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// Prepare WiFi
const char *ssid = "RD_Hunonic_Mesh"; // Replace with your
const char *password = "66668888";    // WiFi credentials

// BLE UUIDs - giống với BLEHUDNaviESP32
#define SERVICE_UUID        "DD3F0AD1-6239-4E1F-81F1-91F6C9F01D86"
#define CHAR_INDICATE_UUID  "DD3F0AD2-6239-4E1F-81F1-91F6C9F01D86"
#define CHAR_WRITE_UUID     "DD3F0AD3-6239-4E1F-81F1-91F6C9F01D86"

// BLE definitions
#define SERVICE_UUID        "DD3F0AD1-6239-4E1F-81F1-91F6C9F01D86"
#define CHAR_INDICATE_UUID  "DD3F0AD2-6239-4E1F-81F1-91F6C9F01D86"
#define CHAR_WRITE_UUID     "DD3F0AD3-6239-4E1F-81F1-91F6C9F01D86"

// Search your next airport here and get the ICAO code
// https://en.wikipedia.org/wiki/ICAO_airport_code
const char *metar = "https://aviationweather.gov/api/data/metar?ids=KDEN&format=json"; // KDEN = Denver
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
int wifiTimeOutCounter = 0;
#define TIMEOFFSET -3600 * 5 // no daylight saving time

// Init parameter for universe simulation - giảm độ phức tạp để tăng hiệu suất
int const n = 3, m = 100; // Giảm số lượng điểm để tránh làm chậm và che mất văn bản
float const r = 0.1;
float x = 0, v = 0, t = 0; // v is used in the loop animation

// Definition for the screen
#define MAX_Y 170
#define MAX_X 320
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft); // Declare Sprite object "spr" with pointer to "tft" object

// Màu sắc - giống với BLEHUDNaviESP32
#define COLOR_BLACK    0x0000
#define COLOR_WHITE    0xFFFF
#define COLOR_MAGENTA  0xF81F
#define COLOR_RED      TFT_RED
#define COLOR_GREEN    TFT_GREEN
#define COLOR_BLUE     TFT_BLUE
#define COLOR_YELLOW   TFT_YELLOW

// Biến canvas - tương tự với g_canvas trong BLEHUDNaviESP32
uint16_t* canvas = NULL;
const int CANVAS_SIZE_BYTES = MAX_X * MAX_Y * sizeof(uint16_t);

// Set global variables for the weather information
int temperature = 0; // °C
int dew_point = 0;   // °C
int wind_speed_knots = 0;
int pressure = 0;          // hPa
int relative_humidity = 0; // %
int wind_speed_kmh = 0;
int data_age_min = 0;
unsigned long epochTime = 0;
unsigned long obsTime = 0;

// Biến lưu thông tin về ESP32 và màn hình
String chipModel;
int chipRevision;
int chipCores;
uint32_t chipId;
String macAddress;
uint64_t uid;
String ipAddress;
String displayInfo = "ST7789 TFT Display";
String displayDriver = "Bodmer TFT_eSPI";
String mcuType = "LilyGO T-Display-S3"; // Tên board dựa trên info.md

// BLE variables - tương tự với BLEHUDNaviESP32
BLEServer* g_pServer = NULL;
BLECharacteristic* g_pCharIndicate = NULL;
bool g_deviceConnected = false;
uint32_t g_lastActivityTime = 0;
bool g_isNaviDataUpdated = false;
std::string g_naviData;

// Button definitions - tương tự với BLEHUDNaviESP32
#define TTGO_LEFT_BUTTON 0
#define GPIO_NUM_TTGO_LEFT_BUTTON GPIO_NUM_0
#define TTGO_RIGHT_BUTTON 35
#define BUTTON_DEEP_SLEEP TTGO_LEFT_BUTTON
#define GPIO_NUM_WAKEUP GPIO_NUM_TTGO_LEFT_BUTTON
bool g_sleepRequested = false;

// Biến hiển thị voltage nếu cần
static bool g_showVoltage = false;

// Update parameter for the weather data
unsigned long previousMillis = 0;
#define INTERVAL 60000 * 5 // 5 min

// Bluetooth event callbacks - giống với BLEHUDNaviESP32
class MyServerCallbacks: public BLEServerCallbacks
{
    void onConnect(BLEServer* pServer) override
    {
        g_deviceConnected = true;
        g_lastActivityTime = millis();
        Serial.println("BLE Client connected");
    }

    void onDisconnect(BLEServer* pServer) override
    {
        g_deviceConnected = false;
        BLEDevice::startAdvertising();
        Serial.println("BLE Client disconnected");
    }
};

class MyCharWriteCallbacks: public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pCharacteristic)
    {
        g_lastActivityTime = millis();
        std::string value = pCharacteristic->getValue();

        if (value.length() > 0)
        {
            g_naviData = value;
            g_isNaviDataUpdated = true;
            Serial.print("New value, length = ");
            Serial.print(value.length());
            Serial.print(": ");
            for (int i = 0; i < value.length(); ++i)
            {
                char tmp[4] = "";
                sprintf(tmp, "%02X ", value[i]);
                Serial.print(tmp);
            }
            Serial.println();
        }
    }
};

// Connect to weather server and get data - simplified
void weatherData()
{
    // Thay vì lấy dữ liệu từ API, sử dụng các giá trị cố định
    temperature = 21;        // °C
    relative_humidity = 49;  // %
    wind_speed_kmh = 22;     // km/h
    pressure = 1023;         // hPa
    data_age_min = 42359135; // min

    // Minimal serial output to save memory
    Serial.println("Weather data loaded (fixed values)");
}

// Hàm vẽ tốc độ - tương tự với BLEHUDNaviESP32
void DrawSpeed(uint8_t speed)
{
    if (speed == 0)
        return;

    char str[4] = {};
    sprintf(str, "%u", (unsigned int)speed);
    
    // Vẽ hình tròn làm nền cho tốc độ
    tft.fillCircle(MAX_X/4, 30, 25, COLOR_WHITE);
    tft.setTextColor(COLOR_BLACK);
    
    // Điều chỉnh vị trí dựa trên số chữ số
    int x = MAX_X/4 - 5;
    if (speed <= 9)
        x = MAX_X/4 - 5;
    else if (speed <= 99)
        x = MAX_X/4 - 10;
    else
        x = MAX_X/4 - 15;
    
    tft.setTextSize(2);
    tft.setCursor(x, 22);
    tft.print(str);
}

// Hàm vẽ hướng di chuyển - tương tự với BLEHUDNaviESP32
void DrawDirection(uint8_t direction)
{
    // Vẽ mũi tên dựa trên hướng
    int centerX = 3*MAX_X/4;
    int centerY = 30;
    int radius = 20;
    
    tft.fillCircle(centerX, centerY, radius, COLOR_BLUE);
    
    // Vẽ mũi tên theo hướng
    int arrowX1, arrowY1, arrowX2, arrowY2, arrowX3, arrowY3;
    
    switch(direction % 8) { // Giản lược để có 8 hướng cơ bản
        case 0: // Thẳng
            arrowX1 = centerX;
            arrowY1 = centerY - radius;
            arrowX2 = centerX - radius/2;
            arrowY2 = centerY + radius/2;
            arrowX3 = centerX + radius/2;
            arrowY3 = centerY + radius/2;
            break;
        case 1: // Phải
            arrowX1 = centerX + radius;
            arrowY1 = centerY;
            arrowX2 = centerX - radius/2;
            arrowY2 = centerY - radius/2;
            arrowX3 = centerX - radius/2;
            arrowY3 = centerY + radius/2;
            break;
        case 2: // Phải gấp
            arrowX1 = centerX + radius;
            arrowY1 = centerY;
            arrowX2 = centerX - radius/3;
            arrowY2 = centerY - radius/2;
            arrowX3 = centerX - radius/3;
            arrowY3 = centerY + radius/2;
            break;
        case 3: // Quay lại phải
            arrowX1 = centerX;
            arrowY1 = centerY + radius;
            arrowX2 = centerX - radius/2;
            arrowY2 = centerY - radius/2;
            arrowX3 = centerX + radius/2;
            arrowY3 = centerY - radius/2;
            break;
        case 4: // Quay lại
            arrowX1 = centerX - radius/2;
            arrowY1 = centerY + radius/2;
            arrowX2 = centerX + radius/2;
            arrowY2 = centerY + radius/2;
            arrowX3 = centerX;
            arrowY3 = centerY - radius/2;
            tft.fillCircle(centerX, centerY, radius/2, COLOR_BLUE);
            break;
        case 5: // Quay lại trái
            arrowX1 = centerX;
            arrowY1 = centerY + radius;
            arrowX2 = centerX - radius/2;
            arrowY2 = centerY - radius/2;
            arrowX3 = centerX + radius/2;
            arrowY3 = centerY - radius/2;
            break;
        case 6: // Trái gấp
            arrowX1 = centerX - radius;
            arrowY1 = centerY;
            arrowX2 = centerX + radius/3;
            arrowY2 = centerY - radius/2;
            arrowX3 = centerX + radius/3;
            arrowY3 = centerY + radius/2;
            break;
        case 7: // Trái
            arrowX1 = centerX - radius;
            arrowY1 = centerY;
            arrowX2 = centerX + radius/2;
            arrowY2 = centerY - radius/2;
            arrowX3 = centerX + radius/2;
            arrowY3 = centerY + radius/2;
            break;
        default:
            return;
    }
    
    tft.fillTriangle(arrowX1, arrowY1, arrowX2, arrowY2, arrowX3, arrowY3, COLOR_WHITE);
}

// This is missing in the library
String getFormattedDate()
{
#define LEAP_YEAR(Y) ((Y > 0) && !(Y % 4) && ((Y % 100) || !(Y % 400)))
    unsigned long rawTime = timeClient.getEpochTime() / 86400L;
    unsigned long days = 0, year = 1970;
    uint8_t month;
    static const uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    while ((days += (LEAP_YEAR(year) ? 366 : 365)) <= rawTime)
        year++;
    rawTime -= days - (LEAP_YEAR(year) ? 366 : 365);
    for (month = 0; month < 12; month++)
    {
        uint8_t monthLength;
        if (month == 1)
            monthLength = LEAP_YEAR(year) ? 29 : 28;
        else
            monthLength = monthDays[month];
        if (rawTime < monthLength)
            break;
        rawTime -= monthLength;
    }
    String monthStr = ++month < 10 ? "0" + String(month) : String(month);
    String dayStr = ++rawTime < 10 ? "0" + String(rawTime) : String(rawTime);
    return String(dayStr) + "-" + monthStr + "-" + year;
}

void setup()
{
    // Entry point
    Serial.begin(115200);
    while (!Serial)
        delay(10);
    Serial.println("BLENaviESP32 setup() started");

    // Thu thập thông tin về ESP32
    chipId = 0;
    for (int i = 0; i < 17; i = i + 8)
        chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
    
    chipModel = ESP.getChipModel();
    chipRevision = ESP.getChipRevision();
    chipCores = ESP.getChipCores();
    
    Serial.printf("ESP32 Chip model = %s Rev %d\n", chipModel.c_str(), chipRevision);
    Serial.printf("This chip has %d cores\n", chipCores);
    
    // Khởi tạo màn hình
    tft.init();
    tft.setRotation(1);        // Đặt rotation
    tft.fillScreen(COLOR_BLACK); // Xóa màn hình
    
    // Khởi tạo canvas
    canvas = new uint16_t[MAX_X * MAX_Y];
    memset(canvas, 0, CANVAS_SIZE_BYTES);
    
    // Hiển thị logo hoặc màn hình khởi động
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 30);
    tft.print("ESP32 HUD Navigation");
    tft.setCursor(10, 60);
    tft.print("Initializing...");
    
    Serial.println("Display init done");

    // Connect to WiFi
    Serial.print("Connecting to WiFi ");
    Serial.print(ssid);
    Serial.print(" ");
    
    WiFi.begin(ssid, password);
    int wifiTimeOutCounter = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
        wifiTimeOutCounter++;
        if (wifiTimeOutCounter >= 60)
            ESP.restart();
    }
    
    Serial.println("");
    Serial.println("WiFi connected.");
    ipAddress = WiFi.localIP().toString();
    Serial.print("IP address: ");
    Serial.println(ipAddress);

    uint8_t mac[6];
    WiFi.macAddress(mac);
    macAddress = "";
    for (int i = 0; i < 6; i++)
    {
        char buf[3];
        sprintf(buf, "%02X", mac[i]);
        macAddress += buf;
        if (i < 5)
            macAddress += ":";
    }
    
    // Convert MAC address to unique identifier (UID)
    uid = ((uint64_t)mac[0] << 40) | ((uint64_t)mac[1] << 32) | ((uint64_t)mac[2] << 24) |
          ((uint64_t)mac[3] << 16) | ((uint64_t)mac[4] << 8) | ((uint64_t)mac[5]);

    // Khởi tạo BLE - giống với BLEHUDNaviESP32
    Serial.println("BLE init started");
    
    BLEDevice::init("ESP32 HUD");
    g_pServer = BLEDevice::createServer();
    g_pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = g_pServer->createService(SERVICE_UUID);

    // characteristic for indicate
    {
        uint32_t charProperties = BLECharacteristic::PROPERTY_INDICATE;
        g_pCharIndicate = pService->createCharacteristic(CHAR_INDICATE_UUID, charProperties);
        g_pCharIndicate->addDescriptor(new BLE2902());
        g_pCharIndicate->setValue("");
    }

    // characteristic for write
    {
        uint32_t charProperties = BLECharacteristic::PROPERTY_WRITE;
        BLECharacteristic *pCharWrite = pService->createCharacteristic(CHAR_WRITE_UUID, charProperties);
        pCharWrite->setCallbacks(new MyCharWriteCallbacks());
    }

    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    Serial.println("BLE init done");

    // Init time system
    timeClient.begin();
    timeClient.setTimeOffset(TIMEOFFSET);
    
    // Lấy thông tin thời tiết
    weatherData();
    
    // Set up deep sleep functionality
    pinMode(TTGO_LEFT_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TTGO_LEFT_BUTTON), []() {
        if (digitalRead(TTGO_LEFT_BUTTON) == LOW) {
            g_sleepRequested = true;
        }
    }, FALLING);
    
    Serial.println("setup() finished");
}

// Hàm chuyển đổi màu từ 4bit sang 16bit - từ BLEHUDNaviESP32
uint16_t Color4To16bit(uint16_t color4bit)
{
    color4bit &= 0x0F;
    uint16_t color16bit = 0;

    const uint16_t maxColor4bit = 0x0F;
    const uint16_t maxColor5bit = 0x1F;
    const uint16_t maxColor6bit = 0x3F;
    
    const uint16_t red   = color4bit * maxColor5bit / maxColor4bit;
    const uint16_t green = color4bit * maxColor6bit / maxColor4bit;
    const uint16_t blue  = color4bit * maxColor5bit / maxColor4bit;

    // color 16 bit: rrrrrggg gggbbbbb
    color16bit |= red << 11;
    color16bit |= green << 5;
    color16bit |= blue;
    
    return color16bit;
}

// Hàm đặt pixel trên canvas
void SetPixelCanvas(int16_t x, int16_t y, uint16_t value)
{
    if (x < 0 || y < 0 || x >= MAX_X || y >= MAX_Y)
    {
        return;
    }
    canvas[y * MAX_X + x] = value;
}

// Hàm vẽ hình chữ nhật trên canvas
void FillRect(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color)
{
    if (x >= MAX_X || y >= MAX_Y || width <= 0 || height <= 0)
    {
        return;
    }

    int16_t xStart = max(x, int16_t(0));
    int16_t yStart = max(y, int16_t(0));
    int16_t xEnd = min(x + width, MAX_X);
    int16_t yEnd = min(y + height, MAX_Y);

    for (int16_t y = yStart; y < yEnd; ++y)
    {
        for (int16_t x = xStart; x < xEnd; ++x)
        {
            SetPixelCanvas(x, y, color);
        }
    }
}

// Hàm cập nhật màn hình từ canvas
void RedrawFromCanvas()
{
    tft.pushImage(0, 0, MAX_X, MAX_Y, canvas);
}

// Hàm vẽ thông báo ở cuối màn hình - tương tự BLEHUDNaviESP32
void DrawBottomMessage(const char* msg, uint16_t color)
{
    const int16_t textHeight = 16;
    const int16_t yOffset = MAX_Y - textHeight;
    FillRect(0, yOffset, MAX_X, textHeight, COLOR_BLACK);
    
    // Vẽ text trực tiếp lên màn hình thay vì dùng sprite
    tft.setTextColor(color);
    tft.setCursor(10, yOffset);
    tft.setTextSize(2);
    tft.print(msg);
}

// Hàm vẽ thông điệp trên canvas
void DrawMessage(const char* msg, int xStart, int yStart, int scale, bool overwrite, uint16_t color)
{
    // Vẽ text trực tiếp lên màn hình
    tft.setTextColor(color);
    tft.setCursor(xStart, yStart);
    tft.setTextSize(scale);
    tft.print(msg);
}

// Xử lý dữ liệu Navigation - tương tự với BLEHUDNaviESP32
void HandleNaviData() {
    if (g_isNaviDataUpdated) {
        g_isNaviDataUpdated = false;
        
        std::string currentData = g_naviData;
        if (currentData.size() > 0) {
            memset(canvas, 0, CANVAS_SIZE_BYTES); // Clear canvas
            
            if (currentData[0] == 1) {
                Serial.print("Processing navigation data: length = ");
                Serial.println(currentData.length());
                
                const int speedOffset = 1;
                const int instructionOffset = 2;
                const int textOffset = 3;

                // Display navigation text if available
                if (currentData.length() > textOffset) {
                    const char* text = currentData.c_str() + textOffset;
                    const int textLen = strlen(text);
                    int scale = 4;
                    
                    if (textLen > 8) {
                        scale = 2;
                    } else if (textLen > 6) {
                        scale = 3;
                    }
                    
                    DrawMessage(text, 10, 64, scale, true, COLOR_WHITE);
                }
                
                // Display direction
                if (currentData.length() > instructionOffset) {
                    uint8_t direction = currentData.c_str()[instructionOffset];
                    DrawMessage(("Direction: " + String(direction)).c_str(), 10, 30, 2, true, COLOR_WHITE);
                }
                
                // Display speed
                if (currentData.length() > speedOffset) {
                    uint8_t speed = currentData.c_str()[speedOffset];
                    DrawMessage(("Speed: " + String(speed) + " km/h").c_str(), 10, 10, 2, true, COLOR_WHITE);
                }
                
                RedrawFromCanvas();
            } else {
                Serial.println("Invalid navigation data format");
                DrawBottomMessage("Invalid data", COLOR_RED);
            }
        }
    }
}

void loop()
{
    // Kiểm tra yêu cầu ngủ sâu
    if (g_sleepRequested)
    {
        DrawBottomMessage("SLEEP", COLOR_MAGENTA);
        delay(1000);
        
        // Chuẩn bị chế độ ngủ sâu
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_WAKEUP, 0);
        delay(200);
        esp_deep_sleep_start();
        return;
    }
    else if (g_showVoltage)
    {
        // Không có chức năng đo điện áp trong ví dụ này
        static uint64_t voltageTimeStamp = 0;
        if (millis() - voltageTimeStamp > 1000)
        {
            voltageTimeStamp = millis();
            String voltageStr = "Batt: 3.7V";  // Giả lập giá trị pin
            DrawBottomMessage(voltageStr.c_str(), COLOR_WHITE);
        }
    }
    else if (g_deviceConnected)
    {
        // Xử lý dữ liệu Navigation BLE
        if (g_isNaviDataUpdated)
        {
            HandleNaviData();
        }
        else
        {
            // Gửi tín hiệu indicate định kỳ để giữ kết nối
            uint32_t time = millis();
            if (time - g_lastActivityTime > 4000)
            {
                g_lastActivityTime = time;
                g_pCharIndicate->indicate();
            }
        }
    }
    else if (millis() > 3000)
    {
        // Nếu không có kết nối BLE, hiển thị thông tin hệ thống
        static uint32_t lastInfoUpdate = 0;
        if (millis() - lastInfoUpdate > 5000)  // Cập nhật mỗi 5 giây
        {
            lastInfoUpdate = millis();
            
            tft.fillScreen(COLOR_BLACK);
            tft.setTextColor(COLOR_WHITE);
            
            // Hiển thị thông tin ESP32
            tft.setTextSize(2);
            tft.setCursor(10, 10);
            tft.print("ESP32 HUD NAVIGATION");
            
            tft.drawLine(10, 30, MAX_X-10, 30, COLOR_WHITE);
            
            tft.setTextSize(1);
            tft.setCursor(10, 40);
            tft.print("Model: ");
            tft.print(chipModel);
            tft.print(" Rev ");
            tft.println(chipRevision);
            
            tft.setCursor(10, 55);
            tft.print("Cores: ");
            tft.println(chipCores);
            
            tft.setCursor(10, 70);
            tft.print("IP: ");
            tft.println(ipAddress);
            
            tft.setCursor(10, 85);
            tft.print("MAC: ");
            tft.println(macAddress);
            
            // Hiển thị thông tin WiFi và thời tiết
            tft.drawLine(10, 100, MAX_X-10, 100, COLOR_WHITE);
            
            tft.setCursor(10, 110);
            tft.print("Weather: ");
            tft.print(temperature);
            tft.print("C, ");
            tft.print(relative_humidity);
            tft.print("%, Wind: ");
            tft.print(wind_speed_kmh);
            tft.println("km/h");
            
            // Hiển thị thông tin thời gian
            timeClient.update();
            tft.setCursor(10, 125);
            tft.print("Time: ");
            tft.println(timeClient.getFormattedTime());
            
            // Hiển thị trạng thái BLE
            tft.drawLine(10, 140, MAX_X-10, 140, COLOR_WHITE);
            tft.setCursor(10, 150);
            tft.print("BLE Status: ");
            tft.setTextColor(COLOR_RED);
            tft.println("Disconnected");
            
            // Hiển thị hướng dẫn
            tft.setTextColor(COLOR_WHITE);
            tft.setCursor(10, MAX_Y-15);
            tft.print("Waiting for BLE connection...");
            
            Serial.println("Disconnected - waiting for BLE connection");
        }
    }
    
    delay(10);
}
